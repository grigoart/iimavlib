/**
 * @file 	SDLDevice.cpp
 *
 * @date 	23.2.2013
 * @author 	Zdenek Travnicek <travnicek@iim.cz>
 * @copyright GNU Public License 3.0
 *
 */


#include "SDL.h"
#include "iimavlib/SDLDevice.h"
#include "iimavlib/Utils.h"
#include "iimavlib/video_ops.h"
#ifdef SYSTEM_LINUX
#include <unistd.h>
#endif
namespace iimavlib {

struct SDLDeleter {
	void operator()(SDL_Surface*obj){SDL_FreeSurface(obj);}
};
struct sdl_pimpl_t {
	std::unique_ptr<SDL_Surface,SDLDeleter> window_;
};

namespace {
	int convert_sdl_keysym_to_key(const SDLKey keysym);
}
SDLDevice::SDLDevice(int width, int height, const std::string& title, bool fullscreen):
		width_(width),height_(height),title_(title),finish_(false),
		data_changed_(false),fullscreen_(fullscreen),flip_required_(false)
{
	static_assert(sizeof(rgb_t)==3,"Wrongly packed RGB struct!");
	
	data_.resize(width,height);
	pimpl_.reset(new sdl_pimpl_t());
}

SDLDevice::~SDLDevice()
{
	stop();
	SDL_Quit();
}

bool SDLDevice::start()
{
	std::unique_lock<std::mutex> lock(thread_mutex_);
	if (thread_.joinable()) return true;
	thread_ = std::thread([=](){this->run();});
	logger[log_level::debug] << "SDL thread started";
	if (thread_.joinable()) return true;
	return false;
}

bool SDLDevice::stop()
{
	std::unique_lock<std::mutex> lock(thread_mutex_);
	if (!thread_.joinable()) return true;
	finish_ = true;
	thread_.join();
	
	logger[log_level::debug] << "SDL thread joined";
	return true;

}

void SDLDevice::run()
{
	SDL_Init(SDL_INIT_VIDEO);
	logger[log_level::debug] << "Creating SDL window";
	int flags = SDL_DOUBLEBUF|(fullscreen_?SDL_FULLSCREEN:0);
	pimpl_->window_.reset(SDL_SetVideoMode(static_cast<int>(width_), static_cast<int>(height_),
			24, flags));
	if (!pimpl_->window_) {
		logger[log_level::fatal] << "Failed to create SDL window!";
		finish_=true;
	} else {
		logger[log_level::debug] << "SDL window created";
	}
	SDL_WM_SetCaption(title_.c_str(),title_.c_str());
	
	while (process_events()) {
		update_data();
		if (flip_required_) {
			SDL_Flip(pimpl_->window_.get());
			flip_required_ = false;
		} else {
			SDL_Delay(5);
		}
	}
	pimpl_->window_.reset();
}
void SDLDevice::update_data()
{
	std::unique_lock<std::mutex> lock(data_mutex_);
	if (data_changed_) {
		SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(&data_.data[0],static_cast<int>(width_),static_cast<int>(height_),24,static_cast<int>(width_*3),0x0000FF,0x00FF00,0xFF0000,0);
		SDL_BlitSurface(surface, nullptr, pimpl_->window_.get(), nullptr);
		SDL_FreeSurface(surface);
		flip_required_ = true;
		data_changed_ = false;
	}
}
bool SDLDevice::process_events()
{
	if(!finish_) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch(event.type) {
				case SDL_KEYDOWN:
				case SDL_KEYUP:
					if (!key_pressed(convert_sdl_keysym_to_key(event.key.keysym.sym),event.key.state == SDL_PRESSED)) {
						finish_ = true;
					} break;
				case SDL_MOUSEBUTTONDOWN:
				case SDL_MOUSEBUTTONUP:
					if (!mouse_button(event.button.button - 1, event.button.state == SDL_PRESSED, event.button.x, event.button.y)) {
						finish_ = true;
					} break;
				case SDL_MOUSEMOTION:
					if (!mouse_moved(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel)) {
						finish_ = true;
					} break;
				case SDL_QUIT:
					finish_ = true;
					logger[log_level::debug] << "Quit event received.";
					break;
				case SDL_VIDEOEXPOSE: {
					logger[log_level::debug] << "Video expose";
					std::unique_lock<std::mutex> lock(data_mutex_);
					data_changed_ = true;
				} break;
			}
		}
	}
	return !finish_;
}

bool SDLDevice::key_pressed(const int key, bool pressed)
{
	//std::unique_lock<std::mutex> lock(data_mutex_);
	return do_key_pressed(key, pressed);
}

bool SDLDevice::do_key_pressed(const int key, bool pressed)
{
	using namespace iimavlib::keys;
	if (pressed && key == key_escape) {
		logger[log_level::debug] << "ESC pressed.";
		return false;
	}
	return true;
}
bool SDLDevice::mouse_moved(const int x, const int y, const int dx, const int dy)
{
	return do_mouse_moved(x, y, dx, dy);
}
bool SDLDevice::mouse_button(const int key, const bool pressed, const int x, const int y)
{
	return do_mouse_button(key, pressed, x, y);
}
bool SDLDevice::do_mouse_moved(const int, const int, const int, const int)
{
	return true;
}
bool SDLDevice::do_mouse_button(const int, const bool, const int, const int)
{
	return true;
}

//template<>
//bool SDLDevice::update(const data_type& data) {
//	if (finish_) return false;
//	std::unique_lock<std::mutex> lock(data_mutex_);
//	if (data.size()>data_.size()) data_.resize(data.size());
//	std::copy(data.begin(),data.end(),data_.begin());
//	data_changed_ = true;
//	return true;
//}

bool SDLDevice::is_stopped() const
{
	return finish_;
}

bool SDLDevice::blit(const video_buffer_t& new_data, rectangle_t position) {
	if (is_stopped()) return false;
	std::unique_lock<std::mutex> lock(data_mutex_);

	iimavlib::blit(data_, new_data, std::move(position));


	data_changed_ = true;
	return true;
}



namespace {
using namespace keys;
#ifdef MODERN_COMPILER
const std::map<SDLKey, int> keysym_to_key = {
{SDLK_BACKSPACE,	key_backspace},
{SDLK_TAB,			key_tab},
{SDLK_RETURN,		key_enter},
{SDLK_PAUSE,		key_pause},
{SDLK_ESCAPE,		key_escape},
{SDLK_SPACE,		key_space},
{SDLK_PLUS,			key_plus},
{SDLK_MINUS,		key_minus},
{SDLK_0,			key_0},
{SDLK_1,			key_1},
{SDLK_2,			key_2},
{SDLK_3,			key_3},
{SDLK_4,			key_4},
{SDLK_5,			key_5},
{SDLK_6,			key_6},
{SDLK_7,			key_7},
{SDLK_8,			key_8},
{SDLK_9,			key_9},
{SDLK_a,			key_a},
{SDLK_b,			key_b},
{SDLK_c,			key_c},
{SDLK_d,			key_d},
{SDLK_e,			key_e},
{SDLK_f,			key_f},
{SDLK_g,			key_g},
{SDLK_h,			key_h},
{SDLK_i,			key_i},
{SDLK_j,			key_j},
{SDLK_k,			key_k},
{SDLK_l,			key_l},
{SDLK_m,			key_m},
{SDLK_n,			key_n},
{SDLK_o,			key_o},
{SDLK_p,			key_p},
{SDLK_q,			key_q},
{SDLK_r,			key_r},
{SDLK_s,			key_s},
{SDLK_t,			key_t},
{SDLK_u,			key_u},
{SDLK_v,			key_v},
{SDLK_w,			key_w},
{SDLK_x,			key_x},
{SDLK_y,			key_y},
{SDLK_z,			key_z},
{SDLK_F1,			key_f1},
{SDLK_F2,			key_f2},
{SDLK_F3,			key_f3},
{SDLK_F4,			key_f4},
{SDLK_F5,			key_f5},
{SDLK_F6,			key_f6},
{SDLK_F7,			key_f7},
{SDLK_F8,			key_f8},
{SDLK_F9,			key_f9},
{SDLK_F10,			key_f10},
{SDLK_F11,			key_f11},
};

#else
const std::map<SDLKey, int> keysym_to_key = InitMap<SDLKey, int>
(SDLK_BACKSPACE,	key_backspace)
(SDLK_TAB,			key_tab)
(SDLK_RETURN,		key_enter)
(SDLK_PAUSE,		key_pause)
(SDLK_ESCAPE,		key_escape)
(SDLK_SPACE,		key_space)
(SDLK_PLUS,			key_plus)
(SDLK_MINUS,		key_minus)
(SDLK_0,			key_0)
(SDLK_1,			key_1)
(SDLK_2,			key_2)
(SDLK_3,			key_3)
(SDLK_4,			key_4)
(SDLK_5,			key_5)
(SDLK_6,			key_6)
(SDLK_7,			key_7)
(SDLK_8,			key_8)
(SDLK_9,			key_9)
(SDLK_a,			key_a)
(SDLK_b,			key_b)
(SDLK_c,			key_c)
(SDLK_d,			key_d)
(SDLK_e,			key_e)
(SDLK_f,			key_f)
(SDLK_g,			key_g)
(SDLK_h,			key_h)
(SDLK_i,			key_i)
(SDLK_j,			key_j)
(SDLK_k,			key_k)
(SDLK_l,			key_l)
(SDLK_m,			key_m)
(SDLK_n,			key_n)
(SDLK_o,			key_o)
(SDLK_p,			key_p)
(SDLK_q,			key_q)
(SDLK_r,			key_r)
(SDLK_s,			key_s)
(SDLK_t,			key_t)
(SDLK_u,			key_u)
(SDLK_v,			key_v)
(SDLK_w,			key_w)
(SDLK_x,			key_x)
(SDLK_y,			key_y)
(SDLK_z,			key_z)
(SDLK_F1,			key_f1)
(SDLK_F2,			key_f2)
(SDLK_F3,			key_f3)
(SDLK_F4,			key_f4)
(SDLK_F5,			key_f5)
(SDLK_F6,			key_f6)
(SDLK_F7,			key_f7)
(SDLK_F8,			key_f8)
(SDLK_F9,			key_f9)
(SDLK_F10,			key_f10)
(SDLK_F11,			key_f11)
(SDLK_F12,			key_f12);

#endif

int convert_sdl_keysym_to_key(const SDLKey keysym)
{
	auto it = keysym_to_key.find(keysym);
	if (it!=keysym_to_key.end()) return it->second;
	return static_cast<int>(keysym);
}
}

}
