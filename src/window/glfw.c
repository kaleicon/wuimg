#include <limits.h>
#include <stdlib.h>

#include "wudefs.h"
#include "window/glfw.h"

static void callback_close(GLFWwindow *wnd) {
	struct glfw_context *glfw = glfwGetWindowUserPointer(wnd);
	glfw->pub->event.program = wu_program_exit;
}

static void callback_focus(GLFWwindow *wnd, const int focused) {
	struct glfw_context *glfw = glfwGetWindowUserPointer(wnd);
	glfw->pub->win.focused = focused;
}

static void callback_framebuffer(GLFWwindow *wnd, const int w, const int h) {
	struct glfw_context *glfw = glfwGetWindowUserPointer(wnd);
	window_size_update(glfw->pub, w, h);
}

static void callback_cursor_pos(GLFWwindow *wnd, const double x, const double y) {
	struct glfw_context *glfw = glfwGetWindowUserPointer(wnd);
	window_cursor_move(glfw->pub, x, y);
}

static void callback_cursor_button(GLFWwindow *wnd, const int button,
const int action, const int mods) {
	(void)mods;

	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		struct glfw_context *glfw = glfwGetWindowUserPointer(wnd);
		glfw->pub->win.pressed = (action == GLFW_PRESS);
//		glfwSetInputMode(window, GLFW_CURSOR,
//			pressed ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	}
}

static void callback_scroll(GLFWwindow *wnd, const double x, const double y) {
	struct glfw_context *glfw = glfwGetWindowUserPointer(wnd);
	window_scroll(&glfw->pub->win.cur, x, y);
}

static void callback_key(GLFWwindow *wnd, const int key, const int scan,
const int action, const int mode) {
	(void)scan;

	enum key_action keyact;
	switch (action) {
	case GLFW_PRESS: keyact = key_press; break;
	case GLFW_RELEASE: keyact = key_release; break;
	default: return;
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
	case GLFW_KEY_W: case GLFW_KEY_C:
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
	case GLFW_KEY_S:
		event = 'S';
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

	case GLFW_KEY_END: event = '='; break;
	case GLFW_KEY_HOME: event = (shift) ? '1' : '0'; break;
	case GLFW_KEY_PAGE_UP:
	case GLFW_KEY_KP_ADD: event = '+'; break;
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
		struct glfw_context *glfw = glfwGetWindowUserPointer(wnd);
		window_key_add(&glfw->pub->held_keys, keyact, event, shift);
	}
}

void glfw_fullscreen(struct glfw_context *glfw, const bool is_fullscreen) {
	struct window_geom *geom = &glfw->geom;
	if (is_fullscreen) {
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
}

const char * glfw_setup(struct glfw_context *glfw, struct window_public *pub) {
	if (!glfwInit()) {
		return "glfwInit() failed";
	}

	*glfw = (struct glfw_context){0};
	struct wu_conf *conf = &pub->image.conf;
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
	if (conf->bg[3] < UCHAR_MAX) {
		glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
	}
#if GLFW_VERSION_MINOR >= 3
	glfwWindowHintString(GLFW_X11_CLASS_NAME, WU_CANON_NAME);
#endif

	GLFWwindow *window = glfwCreateWindow(width, height, WU_CANON_NAME,
		NULL, NULL);
	if (!window) {
		return "Couldn't create GLFW window";
	}

	glfw->pub = pub;
	glfw->window = window;

	glfwMakeContextCurrent(window);
	glfwSetWindowUserPointer(window, glfw);

	glfwSetWindowCloseCallback(window, callback_close);
	glfwSetWindowFocusCallback(window, callback_focus);
	glfwSetFramebufferSizeCallback(window, callback_framebuffer);
	glfwSetCursorPosCallback(window, callback_cursor_pos);
	glfwSetMouseButtonCallback(window, callback_cursor_button);
	glfwSetScrollCallback(window, callback_scroll);
	glfwSetKeyCallback(window, callback_key);
	return NULL;
}
