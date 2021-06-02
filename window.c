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

static void framebuffer_resize(struct window_control *control,
const struct display_dims *dims) {
	struct gl_context *gl = &control->window.gl;
	gl->fb_wh[0] = dims->w;
	gl->fb_wh[1] = dims->h;
	gl_even_view(gl);
	gl_matrix_update(gl, &control->image.state);
}

static void scroll_cycle(struct wu_cycle *cycle, const double offset) {
	if (fpclassify(offset) == FP_NORMAL) {
		cycle->acc = fclampf((float)(cycle->acc + offset), -1.0, +1.0);
		if (cycle->acc == 1.0 || cycle->acc == -1.0) {
			cycle->cycle = (int)cycle->acc;
			cycle->acc = 0;
		}
	}
}

static void focus_callback(GLFWwindow *window, int focused) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->window.ctx.glfw.has_focus = focused;
}

static void framebuffer_callback(GLFWwindow *window, const int w, const int h) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->image.conf.fb = (struct display_dims) {
		.w = (unsigned)w,
		.h = (unsigned)h,
	};
	framebuffer_resize(control, &control->image.conf.fb);
}

static void close_callback(GLFWwindow *window) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->window.event.program = close_window;
}

static void scroll_callback(GLFWwindow *window, const double x_off,
const double y_off) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	scroll_cycle(&control->window.event.file, y_off);
	scroll_cycle(&control->image.state.sub, x_off);
}

static void char_callback(GLFWwindow *window, const unsigned int codepoint) {
	(void)window;
	if (codepoint == '+') {
		event_add(key_external, (unsigned char)codepoint, false);
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
		glfwSetWindowCloseCallback(window, close_callback);
		glfwSetWindowFocusCallback(window, focus_callback);
		glfwSetFramebufferSizeCallback(window, framebuffer_callback);
		glfwSetKeyCallback(window, key_callback);
		glfwSetCharCallback(window, char_callback);
		glfwSetScrollCallback(window, scroll_callback);
		glfwSwapInterval(1);
	}
	return window;
}

void window_terminate(struct window_context *window) {
	if (window->backend == window_glfw) {
		glfwTerminate();
	} else {
		kms_terminate(&window->ctx.kms);
	}
}

void window_poll(const struct window_context *window) {
	if (window->backend == window_glfw) {
		glfwPollEvents();
	}
}

void window_draw(struct window_context *window) {
	gl_draw();
	if (window->backend == window_glfw) {
		glfwSwapBuffers(window->ctx.glfw.window);
	} else {
		kms_swap_buffers(&window->ctx.kms);
	}
}

bool window_has_focus(const struct window_context *window) {
	if (window->backend == window_glfw) {
		return window->ctx.glfw.has_focus;
	}
	return true;
}

void window_toggle_fullscreen(struct window_context *window) {
	if (window->backend != window_glfw) {
		return;
	}

	struct glfw_window *glfw = &window->ctx.glfw;
	if (glfw->fullscreen) {
		glfwSetWindowMonitor(glfw->window, NULL,
			glfw->x, glfw->y, glfw->w, glfw->h, GLFW_DONT_CARE);
	} else {
		// Save window dimensions
		glfwGetWindowPos(glfw->window, &glfw->x, &glfw->y);
		glfwGetWindowSize(glfw->window, &glfw->w, &glfw->h);

		GLFWmonitor *monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode *mode = glfwGetVideoMode(monitor);
		glfwSetWindowMonitor(glfw->window, monitor, 0, 0,
			mode->width, mode->height, mode->refreshRate);
	}

	glfw->fullscreen = !glfw->fullscreen;
}

void window_set_title(const struct window_context *window, const char *title) {
	if (window->backend == window_glfw) {
		glfwSetWindowTitle(window->ctx.glfw.window, title);
	}
}

void window_postgl_setup(struct window_control *control) {
	if (control->window.backend == window_glfw) {
		glfwPollEvents();
	} else {
		framebuffer_resize(control, &control->image.conf.fb);
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
		if (kms_setup(&window->ctx.kms, &conf->fb)) {
			window->backend = window_kms;
			return true;
		}
	}
	return false;
}
