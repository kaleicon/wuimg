#include <limits.h>
#include <math.h>

#include <epoxy/egl.h>

#include "wudefs.h"
#include "common.h"
#include "window.h"
#include "opengl.h"
#include "events.h"

#define GLFW 1
#define SDL 2
#define BACKEND GLFW

#if BACKEND == GLFW
#include <GLFW/glfw3.h>
#elif BACKEND == SDL
#include <SDL2/SDL.h>
#endif

static void framebuffer_resize(struct window_control *control, const int w,
const int h) {
	control->context.fb.w = w;
	control->context.fb.h = h;
	even_gl_view(&control->context);
	update_gl_matrix(&control->context, &control->state);

	control->conf.fb.w = (unsigned short)w;
	control->conf.fb.h = (unsigned short)h;
}

#if BACKEND == GLFW
static void scroll_to_cycle(struct wu_pos *pos, const double offset) {
	if (fpclassify(offset) == FP_NORMAL) {
		const float mag = fclampf((float)offset, -1.0, +1.0);
		if (mag == +1.0f || mag == -1.0f) {
			pos->cycle = (int)mag;
			pos->acc = 0.0f;
		} else {
			pos->acc = fclampf(pos->acc + mag, -1.0, +1.0);
			if (pos->acc == +1.0f || pos->acc == -1.0f) {
				pos->cycle = (int)pos->acc;
				pos->acc = 0.0f;
			}
		}
	}
}

static void focus_callback(GLFWwindow *window, int focused) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->screen.has_focus = focused;
}

static void framebuffer_callback(GLFWwindow *window, const int w, const int h) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	framebuffer_resize(control, w, h);
}

static void close_callback(GLFWwindow *window) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->event.program = close_window;
}

static void scroll_callback(GLFWwindow *window, const double x_off,
const double y_off) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	scroll_to_cycle(&control->file, y_off);
	scroll_to_cycle(&control->state.sub, x_off);
}

static void char_callback(GLFWwindow *window, const unsigned int codepoint) {
	(void)window;
	switch (codepoint) {
	case '+': case '-':
		add_event(key_external, codepoint);
	}
}

static void key_callback(GLFWwindow *_w, int key, int _scan, int action,
int mode) {
	(void)_w;
	(void)_scan;

	enum key_action keyact;
	if (action == GLFW_PRESS) {
		keyact = key_press;
	} else if (action == GLFW_RELEASE) {
		keyact = key_release;
	} else {
		return;
	}

	unsigned char event = 0;
	switch (key) {
	case GLFW_KEY_Q: case GLFW_KEY_ESCAPE:
		event = 'q';
		break;
	case GLFW_KEY_F4:
		if (mode & GLFW_MOD_ALT) {
			event = 'q';
		}
		break;
	case GLFW_KEY_W:
		if (mode & GLFW_MOD_CONTROL) {
			event = 'q';
		}
		break;

	case GLFW_KEY_F: case GLFW_KEY_F11:
		event = 'f';
		break;

	case GLFW_KEY_N:
		event = (mode & GLFW_MOD_SHIFT) ? 'N' : 'n';
		break;
	case GLFW_KEY_P:
		event = (mode & GLFW_MOD_SHIFT) ? 'P' : 'p';
		break;

	case GLFW_KEY_COMMA:
		event = (mode & GLFW_MOD_SHIFT) ? ';' : ',';
		break;
	case GLFW_KEY_PERIOD:
		event = (mode & GLFW_MOD_SHIFT) ? ':' : '.';
		break;
	case GLFW_KEY_SPACE: event = ' '; break;

	case GLFW_KEY_R: case GLFW_KEY_F5:
		event = 'r';
		break;

	case GLFW_KEY_D:
		event = (mode & GLFW_MOD_SHIFT) ? 'D' : 'd';
		break;
	case GLFW_KEY_U:
		event = 'u';
		break;

	case GLFW_KEY_H: case GLFW_KEY_LEFT:
		event = 'h';
		break;
	case GLFW_KEY_J: case GLFW_KEY_DOWN:
		event = 'j';
		break;
	case GLFW_KEY_K: case GLFW_KEY_UP:
		event = 'k';
		break;
	case GLFW_KEY_L: case GLFW_KEY_RIGHT:
		event = 'l';
		break;

	case GLFW_KEY_Z: event = 'z'; break;
	case GLFW_KEY_X: event = 'x'; break;
	case GLFW_KEY_I: event = 'i'; break;
	case GLFW_KEY_O: event = 'o'; break;

	case GLFW_KEY_END: event = '0'; break;
	case GLFW_KEY_HOME: event = '1'; break;
	case GLFW_KEY_KP_ADD:
	case GLFW_KEY_PAGE_UP: event = '+'; break;
	case GLFW_KEY_PAGE_DOWN:
	case GLFW_KEY_KP_SUBTRACT: event = '-'; break;

	case GLFW_KEY_0: case GLFW_KEY_KP_0: event = '0'; break;
	case GLFW_KEY_1: case GLFW_KEY_KP_1: event = '1'; break;
	case GLFW_KEY_2: case GLFW_KEY_KP_2: event = '2'; break;
	case GLFW_KEY_3: case GLFW_KEY_KP_3: event = '3'; break;
	case GLFW_KEY_4: case GLFW_KEY_KP_4: event = '4'; break;
	case GLFW_KEY_5: case GLFW_KEY_KP_5: event = '5'; break;
	case GLFW_KEY_6: case GLFW_KEY_KP_6: event = '6'; break;
	case GLFW_KEY_7: case GLFW_KEY_KP_7: event = '7'; break;
	case GLFW_KEY_8: case GLFW_KEY_KP_8: event = '8'; break;
	case GLFW_KEY_9: case GLFW_KEY_KP_9: event = '9'; break;
	}

	if (event) {
		add_event(keyact, event);
	}
}

static void * glfw_setup_window(struct window_control *control, const int window_w,
const int window_h, int *refresh_rate) {
	struct gl_context *context = &control->context;
	const struct wu_conf *conf = &control->conf;
	if (!glfwInit()) {
		return NULL;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
//	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	if (conf->no_window_decorations) {
		glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	}
	if (conf->bg[3] != UCHAR_MAX) {
		glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
	}
#if GLFW_VERSION_MINOR >= 3
	glfwWindowHintString(GLFW_X11_CLASS_NAME, WU_CANON_NAME);
#endif
	GLFWwindow *window = glfwCreateWindow(window_w, window_h,
		WU_CANON_NAME, NULL, NULL);
	glfwMakeContextCurrent(window);

	glfwSetWindowUserPointer(window, control);
	glfwSetWindowCloseCallback(window, close_callback);
	glfwSetWindowFocusCallback(window, focus_callback);
	glfwSetFramebufferSizeCallback(window, framebuffer_callback);
	glfwSetKeyCallback(window, key_callback);
	glfwSetCharCallback(window, char_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSwapInterval(1);

	glfwGetFramebufferSize(window, &context->fb.w, &context->fb.h);

	const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	*refresh_rate = mode->refreshRate;
	return window;
}
#elif BACKEND == SDL
static void sdl_keyevent(const enum key_action keyact,
const SDL_Keysym keysym) {
	unsigned event = 0;
	switch (keysym.sym) {
	case SDLK_q: case SDLK_ESCAPE:
		event = 'q';
		break;
	case SDLK_F4:
		if (keysym.mod & KMOD_ALT) {
			event = 'q';
		}
		break;
	case SDLK_w:
		if (keysym.mod & KMOD_CTRL) {
			event = 'q';
		}
		break;

	case SDLK_f: case SDLK_F11:
		event = 'f';
		break;

	case SDLK_n: event = (keysym.mod & KMOD_SHIFT) ? 'N' : 'n'; break;
	case SDLK_p: event = (keysym.mod & KMOD_SHIFT) ? 'P' : 'p'; break;

	case SDLK_SPACE: event = ' '; break;
	case SDLK_COMMA: event = ','; break;
	case SDLK_PERIOD: event = '.'; break;

	case SDLK_r: case SDLK_F5:
		event = 'r';
		break;

	case SDLK_d:
		event = (keysym.mod & KMOD_SHIFT) ? 'D' : 'd';
		break;
	case SDLK_u: case SDLK_UNDO:
		event = 'u';
		break;

	case SDLK_h: case SDLK_LEFT:
		event = 'h';
		break;
	case SDLK_j: case SDLK_DOWN:
		event = 'j';
		break;
	case SDLK_k: case SDLK_UP:
		event = 'k';
		break;
	case SDLK_l: case SDLK_RIGHT:
		event = 'l';
		break;

	case SDLK_z: event = 'z'; break;
	case SDLK_x: event = 'x'; break;
	case SDLK_i: event = 'i'; break;
	case SDLK_o: event = 'o'; break;

	case SDLK_END: event = '0'; break;
	case SDLK_HOME: event = '1'; break;
	case SDLK_KP_PLUS:
	case SDLK_PAGEUP: event = '+'; break;
	case SDLK_PAGEDOWN:
	case SDLK_KP_MINUS: event = '-'; break;

	case SDLK_0: case SDLK_KP_0: event = '0'; break;
	case SDLK_1: case SDLK_KP_1: event = '1'; break;
	case SDLK_2: case SDLK_KP_2: event = '2'; break;
	case SDLK_3: case SDLK_KP_3: event = '3'; break;
	case SDLK_4: case SDLK_KP_4: event = '4'; break;
	case SDLK_5: case SDLK_KP_5: event = '5'; break;
	case SDLK_6: case SDLK_KP_6: event = '6'; break;
	case SDLK_7: case SDLK_KP_7: event = '7'; break;
	case SDLK_8: case SDLK_KP_8: event = '8'; break;
	case SDLK_9: case SDLK_KP_9: event = '9'; break;
	}

	if (event) {
		add_event(keyact, event);
	}
}

void sdl_mousewheel(struct window_control *control, const int x, const int y) {
	control->state.sub.cycle = iclamp(x, -1, 1);
	control->file.cycle = iclamp(y, -1, 1);
}

static void sdl_winevent(struct window_control *control,
const SDL_WindowEvent event) {
	switch (event.event) {
	case SDL_WINDOWEVENT_SIZE_CHANGED:
		;int w, h;
		SDL_GL_GetDrawableSize(control->window, &w, &h);
		framebuffer_resize(control, w, h);
		break;
	}
}

static void * sdl_winsetup(const struct wu_conf *conf, const int window_w,
const int window_h, int *refresh_rate, struct gl_context *context) {
	SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
	if (SDL_Init(SDL_INIT_VIDEO)) {
		puts(SDL_GetError());
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
		SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
		SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	if (conf->bg[3] != UCHAR_MAX) {
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 1);
	}
	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	if (conf->no_window_decorations) {
		flags |= SDL_WINDOW_BORDERLESS;
	}

	SDL_Window *window = SDL_CreateWindow(WU_CANON_NAME,
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		window_w, window_h, flags);
	if (!window) {
		puts(SDL_GetError());
		return false;
	}

	if (!SDL_GL_CreateContext(window)) {
		puts(SDL_GetError());
		return false;
	}
	SDL_GL_SetSwapInterval(1);
	SDL_GL_GetDrawableSize(window, &context->fb.w, &context->fb.h);

	SDL_DisplayMode mode;
	SDL_GetCurrentDisplayMode(0, &mode);
	*refresh_rate = mode.refresh_rate;
	return window;
}
#endif

void terminate_window(void) {
#if BACKEND == GLFW
	glfwTerminate();
#elif BACKEND == SDL
	SDL_Quit();
#endif
}

void poll_window(struct window_control *control) {
#if BACKEND == GLFW
	(void)control;
	glfwPollEvents();
#elif BACKEND == SDL
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_MOUSEWHEEL:
			sdl_mousewheel(control, event.wheel.x, event.wheel.y);
			break;
		case SDL_KEYDOWN:
			sdl_keyevent(key_press, event.key.keysym);
			break;
		case SDL_KEYUP:
			sdl_keyevent(key_release, event.key.keysym);
			break;
		case SDL_WINDOWEVENT:
			sdl_winevent(control, event.window);
			break;
		case SDL_QUIT:
			control->event.program = close_window;
			break;
		}
	}
#endif
}

void redraw_window(void *window) {
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
#if BACKEND == GLFW
	glfwSwapBuffers(window);
#elif BACKEND == SDL
	SDL_GL_SwapWindow(window);
#endif
}

void set_window_title(void *window, const char *filename) {
#if BACKEND == GLFW
	glfwSetWindowTitle(window, filename);
#elif BACKEND == SDL
	SDL_SetWindowTitle(window, filename);
#endif
}

void set_fullscreen_window(struct window_control *control) {
	struct screen_properties *screen = &control->screen;

#if BACKEND == GLFW
	struct window_geometry *geom = &screen->geom;
	if (screen->fullscreen) {
		glfwSetWindowMonitor(control->window, NULL,
			geom->x, geom->y, geom->w, geom->h, GLFW_DONT_CARE);
	} else {
		// Save window dimensions
		glfwGetWindowPos(control->window, &geom->x, &geom->y);
		glfwGetWindowSize(control->window, &geom->w, &geom->h);

		GLFWmonitor *monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode *mode = glfwGetVideoMode(monitor);
		glfwSetWindowMonitor(control->window, monitor, 0, 0,
			mode->width, mode->height, mode->refreshRate);
	}
#elif BACKEND == SDL
	const unsigned set = screen->fullscreen ? 0 : SDL_WINDOW_FULLSCREEN;
	SDL_SetWindowFullscreen(control->window, set);
#endif
	screen->fullscreen = !screen->fullscreen;
	control->event.window = 0;
}

bool setup_window(struct window_control *control) {
	struct wu_conf *conf = &control->conf;

	const int window_w = conf->initial_size.w ? conf->initial_size.w : 640;
	const int window_h = conf->initial_size.h ? conf->initial_size.h : 480;

#if BACKEND == GLFW
	control->window = glfw_setup_window(control, window_w, window_h,
		&control->screen.refresh_rate);
#elif BACKEND == SDL
	control->window = sdl_winsetup(conf, window_w, window_h,
		&control->screen.refresh_rate, &control->context);
#endif
	if (control->window) {
		control->screen.has_focus = true;
		control->screen.fullscreen = false;
		return true;
	}
	return false;
}
