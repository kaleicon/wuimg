#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"
#include "window.h"
#include "events.h"
#include "drm.h"

#include <GLFW/glfw3.h>

static void framebuffer_resize(struct window_control *control, const int w,
const int h) {
	struct gl_context *gl = &control->window.gl;
	gl_viewport(gl, w, h);
}

static void scroll_cycle(struct wu_cycle *cycle, const double offset) {
	if (fpclassify(offset) == FP_NORMAL) {
		cycle->acc += (float)offset;
		if (cycle->acc >= 1.0 || cycle->acc <= -1.0) {
			cycle->cycle = iclamp((int)cycle->acc, -1, 1);
			cycle->acc = 0;
		}
	}
}

/* GLFW callbacks */
static void callback_close(GLFWwindow *window) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->window.event.program = close_window;
}

static void callback_focus(GLFWwindow *window, const int focused) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->window.ctx.glfw.has_focus = focused;
}

static void callback_framebuffer(GLFWwindow *window, const int w, const int h) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->image.conf.fb = (struct display_dims) {
		.w = (unsigned)w,
		.h = (unsigned)h,
	};
	framebuffer_resize(control, w, h);
}
__attribute__((unused))
static void callback_cursor_pos(GLFWwindow *window, const double x, const double y) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	struct glfw_context *glfw = &control->window.ctx.glfw;

	struct window_cursor *cursor = &glfw->cursor;
	if (cursor->pressed) {
		struct wu_state *state = &control->image.state;
		state->x_offset += (float)(x - cursor->x) / state->zoom;
		state->y_offset += (float)(y - cursor->y) / state->zoom;
		control->window.gl.update_matrix = true;
	} else if (cursor->pressed == 1) {
		++cursor->pressed;
	}
	cursor->x = (float)x;
	cursor->y = (float)y;
}

static void callback_cursor_button(GLFWwindow *window, const int button,
const int action, const int mods) {
	(void)mods;

	struct window_control *control = glfwGetWindowUserPointer(window);
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		const bool pressed = action == GLFW_PRESS;
		control->window.ctx.glfw.cursor.pressed = pressed;
		glfwSetInputMode(window, GLFW_CURSOR,
			pressed ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	}
}

static void callback_scroll(GLFWwindow *window, const double x_off,
const double y_off) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	scroll_cycle(&control->window.event.file, y_off);
	scroll_cycle(&control->image.state.sub, x_off);
}

static void callback_char(GLFWwindow *window, const unsigned int codepoint) {
	(void)window;
	if (codepoint == '+') {
		event_add(key_external, (unsigned char)codepoint, false);
	}
}

static void callback_key(GLFWwindow *w, const int key, const int scan,
const int action, const int mode) {
	(void)w;
	(void)scan;

	enum key_action keyact;
	if (action == GLFW_PRESS) {
		keyact = key_press;
	} else if (action == GLFW_RELEASE) {
		keyact = key_release;
	} else {
		return;
	}

	const bool shift = mode & GLFW_MOD_SHIFT;
	unsigned char event = 0;
	switch (key) {
	case GLFW_KEY_Q: case GLFW_KEY_ESCAPE:
		event = 'Q';
		break;
	case GLFW_KEY_F4:
		if (mode & GLFW_MOD_ALT) {
			event = 'Q';
		}
		break;
	case GLFW_KEY_W:
		if (mode & GLFW_MOD_CONTROL) {
			event = 'Q';
		}
		break;

	case GLFW_KEY_F: case GLFW_KEY_F11:
		event = 'F';
		break;
	case GLFW_KEY_A:
		event = 'A';
		break;
	case GLFW_KEY_M:
		event = 'M';
		break;

	case GLFW_KEY_N:
		event = 'N';
		break;
	case GLFW_KEY_P:
		event = 'P';
		break;

	case GLFW_KEY_COMMA:
		event = shift ? ';' : ',';
		break;
	case GLFW_KEY_PERIOD:
		event = shift ? ':' : '.';
		break;
	case GLFW_KEY_SPACE: event = ' '; break;

	case GLFW_KEY_R: case GLFW_KEY_F5:
		event = 'R';
		break;

	case GLFW_KEY_D:
		event = 'D';
		break;
	case GLFW_KEY_U:
		event = 'U';
		break;

	case GLFW_KEY_H: case GLFW_KEY_LEFT:
		event = 'H';
		break;
	case GLFW_KEY_J: case GLFW_KEY_DOWN:
		event = 'J';
		break;
	case GLFW_KEY_K: case GLFW_KEY_UP:
		event = 'K';
		break;
	case GLFW_KEY_L: case GLFW_KEY_RIGHT:
		event = 'L';
		break;

	case GLFW_KEY_Z: event = 'Z'; break;
	case GLFW_KEY_X: event = 'X'; break;
	case GLFW_KEY_I: event = 'I'; break;
	case GLFW_KEY_O: event = 'O'; break;

	case GLFW_KEY_END: event = '0'; break;
	case GLFW_KEY_HOME: event = '1'; break;
	case GLFW_KEY_KP_ADD:
	case GLFW_KEY_PAGE_UP: event = '+'; break;
	case GLFW_KEY_MINUS:
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
		event_add(keyact, event, shift);
	}
}

static void glfw_toggle_fullscreen(struct window_context *window) {
	struct glfw_context *glfw = &window->ctx.glfw;
	struct window_geom *geom = &glfw->geom;
	if (glfw->fullscreen) {
		glfwSetWindowMonitor(glfw->window, NULL,
			geom->x, geom->y,
			geom->w, geom->h, GLFW_DONT_CARE);
	} else {
		// Save window dimensions
		glfwGetWindowPos(glfw->window, &geom->x, &geom->y);
		glfwGetWindowSize(glfw->window, &geom->w, &geom->h);

		GLFWmonitor *monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode *mode = glfwGetVideoMode(monitor);
		glfwSetWindowMonitor(glfw->window, monitor, 0, 0,
			mode->width, mode->height, mode->refreshRate);
	}

	glfw->fullscreen = !glfw->fullscreen;
}

static GLFWwindow * glfw_setup_window(struct window_control *control,
const struct wu_conf *conf) {
	if (!glfwInit()) {
		return NULL;
	}

	int width = (int)conf->initial_size.w;
	int height = (int)conf->initial_size.h;
	if (!width || !height) {
		int default_w = 640;
		int default_h = 480;
		GLFWmonitor *monitor = glfwGetPrimaryMonitor();
		if (monitor) {
			const GLFWvidmode *video = glfwGetVideoMode(monitor);
			if (video) {
				default_w = video->width;
				default_h = video->height;
			}
		}
		if (!width) {
			width = default_w;
		}
		if (!height) {
			height = default_h;
		}
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, WU_GL_MAJOR);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, WU_GL_MINOR);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
	glfwWindowHint(GLFW_DEPTH_BITS, 0);
	glfwWindowHint(GLFW_STENCIL_BITS, 0);
	if (conf->no_window_decorations) {
		glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	}
	if (conf->bg[3] != UCHAR_MAX) {
		glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
	}
#if GLFW_VERSION_MINOR >= 3
	glfwWindowHintString(GLFW_X11_CLASS_NAME, WU_CANON_NAME);
#endif

	GLFWwindow *window = glfwCreateWindow(width, height, WU_CANON_NAME,
		NULL, NULL);
	if (window) {
		glfwMakeContextCurrent(window);
		glfwSetWindowUserPointer(window, control);

		glfwSetWindowCloseCallback(window, callback_close);
		glfwSetWindowFocusCallback(window, callback_focus);
		glfwSetFramebufferSizeCallback(window, callback_framebuffer);
		glfwSetCursorPosCallback(window, callback_cursor_pos);
		glfwSetMouseButtonCallback(window, callback_cursor_button);
		glfwSetScrollCallback(window, callback_scroll);
		glfwSetCharCallback(window, callback_char);
		glfwSetKeyCallback(window, callback_key);

		glfwSwapInterval(1);
	}
	return window;
}

void window_terminate(struct window_context *window) {
	switch (window->backend) {
	case window_glfw:
		glfwTerminate();
		break;
	case window_drm:
		drm_terminate(&window->ctx.drm);
		break;
	}
}

void window_event(struct window_context *window) {
	switch (window->event.window) {
	case toggle_fullscreen:
		if (window->backend == window_glfw) {
			glfw_toggle_fullscreen(window);
		}
		break;
	case toggle_alpha:
		gl_alpha_toggle(&window->gl);
		break;
	}
}

void window_poll(const struct window_context *window) {
	switch (window->backend) {
	case window_glfw:
		glfwPollEvents();
/*		GLFWwindow *win = window->ctx.glfw.window;
	struct window_control *control = glfwGetWindowUserPointer(win);
	struct glfw_context *glfw = &control->window.ctx.glfw;

	struct window_cursor *cursor = &glfw->cursor;
		double x, y;
		glfwGetCursorPos(win, &x, &y);
//		if (cursor->pressed) {
		if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT)) {
			struct wu_state *state = &control->image.state;
			state->x_offset += (float)(x - cursor->x);
			state->y_offset -= (float)(y - cursor->y);
			control->window.gl.update_matrix = true;
		}
		cursor->x = (float)x;
		cursor->y = (float)y;*/
		break;
	case window_drm:
		break;
	}
}

void window_draw(struct window_context *window) {
	gl_draw();
	switch (window->backend) {
	case window_glfw:
		glfwSwapBuffers(window->ctx.glfw.window);
		break;
	case window_drm:
		drm_swap_buffers(&window->ctx.drm);
		break;
	}
}

bool window_has_focus(const struct window_context *window) {
	switch (window->backend) {
	case window_glfw:
		return window->ctx.glfw.has_focus;
	case window_drm:
		break;
	}
	return true;
}

void window_set_title(const struct window_context *window, const char *title) {
	switch (window->backend) {
	case window_glfw:
		glfwSetWindowTitle(window->ctx.glfw.window, title);
		break;
	case window_drm:
		break;
	}
}

void window_postgl_setup(struct window_control *control) {
	switch (control->window.backend) {
	case window_glfw:
		glfwPollEvents();
		break;
	case window_drm:
		;const struct display_dims *dims = &control->image.conf.fb;
		framebuffer_resize(control, (int)dims->w, (int)dims->h);
		break;
	}
}

bool window_setup(struct window_control *control) {
	struct window_context *window = &control->window;
	struct wu_conf *conf = &control->image.conf;

	if (getenv("WAYLAND_DISPLAY") || getenv("DISPLAY")) {
		window->ctx.glfw.window = glfw_setup_window(control, conf);
		if (window->ctx.glfw.window) {
			window->backend = window_glfw;
			window->ctx.glfw.has_focus = true;
			window->ctx.glfw.fullscreen = false;
			return true;
		}
	} else {
		if (drm_init(&window->ctx.drm, &conf->fb)) {
			window->backend = window_drm;
			return true;
		}
	}
	return false;
}
