#include <stdio.h>
#include <string.h>
#include <math.h>

#include <epoxy/gl.h>
#include <GLFW/glfw3.h>

#include "wudefs.h"
#include "common.h"
#include "window.h"
#include "opengl.h"

static void toggle_fullscreen(GLFWwindow *window,
struct window_control *control) {
	if (control->fullscreen) {
		const struct window_geometry *win = &control->windowed_state;
		glfwSetWindowMonitor(window, NULL,
			win->xpos, win->ypos,
			win->width, win->height, GLFW_DONT_CARE);
	} else {
		GLFWmonitor *monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode *mode = glfwGetVideoMode(monitor);
		glfwSetWindowMonitor(window, monitor, 0, 0, mode->width,
			mode->height, mode->refreshRate);
	}
	control->fullscreen = !control->fullscreen;
}

static void windowpos_callback(GLFWwindow *window, const int x, const int y) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	if (!control->fullscreen) {
		control->windowed_state.xpos = x;
		control->windowed_state.ypos = y;
	}
}

static void windowsize_callback(GLFWwindow *window, const int w, const int h) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	if (!control->fullscreen) {
		control->windowed_state.width = w;
		control->windowed_state.height = h;
	}
}

static void framebuffer_callback(GLFWwindow *window, const int w, const int h) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	correct_gl_view(control->context, w, h);
	update_gl_matrix(control->context);
}

static void manual_framebuffer_callback(GLFWwindow *window,
struct gl_context *context) {
	int fb_w, fb_h;
	glfwGetFramebufferSize(window, &fb_w, &fb_h);
	correct_gl_view(context, fb_w, fb_h);
	update_gl_matrix(context);
}

static void key_callback(GLFWwindow *window, int key,
__attribute__((unused))int scan, int action, __attribute__((unused))int mode) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	struct gl_context *context = control->context;
	if (action != GLFW_RELEASE) {
		switch (key) {
		// Window
		case GLFW_KEY_Q: case GLFW_KEY_ESCAPE:
			glfwSetWindowShouldClose(window, GLFW_TRUE);
			return;
		case GLFW_KEY_F: case GLFW_KEY_F11:
			toggle_fullscreen(window, control);
			return;
		// Movement
		case GLFW_KEY_H: case GLFW_KEY_LEFT:
			change_gl_offset(context, MV_FACTOR, 0);
			return;
		case GLFW_KEY_L: case GLFW_KEY_RIGHT:
			change_gl_offset(context, -MV_FACTOR, 0);
			return;
		case GLFW_KEY_J: case GLFW_KEY_DOWN:
			change_gl_offset(context, 0, MV_FACTOR);
			return;
		case GLFW_KEY_K: case GLFW_KEY_UP:
			change_gl_offset(context, 0, -MV_FACTOR);
			return;
		// Zoom. Bigger values result in smaller images
		case GLFW_KEY_PAGE_UP:
			set_gl_scaling(context, context->trans.scale * 0.5f);
			return;
		case GLFW_KEY_PAGE_DOWN:
			set_gl_scaling(context, context->trans.scale * 2);
			return;
		case GLFW_KEY_1: case GLFW_KEY_HOME:
			set_gl_scaling(context, 1);
			return;
		case GLFW_KEY_0: case GLFW_KEY_END:
			reset_gl_offset(context);
			set_gl_scaling(context, context->fit_zoom);
			return;
		// Rotation. Positive is counter-clockwise
		case GLFW_KEY_Z:
			change_gl_rotation(context, 1);
			manual_framebuffer_callback(window, context);
			return;
		case GLFW_KEY_X:
			change_gl_rotation(context, -1);
			manual_framebuffer_callback(window, context);
			return;
		// Mirror
		case GLFW_KEY_I: // Horizontally
			context->mirror ^= 1;
			change_gl_rotation(context, 2);
			manual_framebuffer_callback(window, context);
			return;
		case GLFW_KEY_O: // Vertically
			context->mirror ^= 1;
			calc_gl_mirrot(context);
			manual_framebuffer_callback(window, context);
			return;
		// Delete
		case GLFW_KEY_D:
			if (!control->rm) {
				print_temp_line("File marked for deletion. "
					"(u to undo)");
				control->rm = true;
			} else {
				++control->cycle;
			}
			return;
		// Undo delete
		case GLFW_KEY_U:
			if (control->rm) {
				print_temp_line("Undone.");
				control->rm = false;
			}
			return;
		// Change sub-image
		case GLFW_KEY_PERIOD:
			++control->cycle_sub_img;
			control->anim = paused;
			return;
		case GLFW_KEY_COMMA:
			--control->cycle_sub_img;
			control->anim = paused;
			return;
		case GLFW_KEY_SPACE:
			control->anim ^= 2;
			return;
		// Refresh file
		case GLFW_KEY_R: case GLFW_KEY_F5:
			control->reload = true;
			return;
		// Change file
		case GLFW_KEY_N:
			++control->cycle;
			break;
		case GLFW_KEY_P:
			--control->cycle;
			break;
		}

		if (action == GLFW_REPEAT) {
			switch (key) {
			case GLFW_KEY_N:
			case GLFW_KEY_P:
				control->cycle_wait = true;
			}
		}
	} else {
		switch (key) {
		case GLFW_KEY_N:
		case GLFW_KEY_P:
			control->cycle_wait = false;
		}
	}
}

bool update_window(GLFWwindow *window, struct window_control *control,
const struct image_file *file, const int idx, const bool first_load) {
	struct gl_context *context = control->context;
	struct raw_img *img = file->sub_img + idx;

	control->cycle = 0;
	control->cycle_sub_img = 0;
	control->rm = false;
	control->reload = false;
	if (first_load) {
		glfwSetWindowTitle(window, file->name);
		if (file->is_animation) {
			control->anim = playing;
		} else {
			control->anim = none;
		}
	}

	if (is_texture_reusable(img, context)) {
		update_gl_texture(img);
		return true;
	} else if (load_gl_texture(img, context)) {
		reset_gl_matrix(context);

		context->rotate = img->rotate;
		context->mirror = img->mirror;
		calc_gl_mirrot(context);

		int fb_w, fb_h;
		glfwGetFramebufferSize(window, &fb_w, &fb_h);
		correct_gl_view(context, fb_w, fb_h);
		context->trans.scale = fmaxf(1, context->fit_zoom);

		update_gl_matrix(context);
		return true;
	}
	return false;
}

void setup_window(GLFWwindow *window, struct window_control *control) {
	memset(control, 0, sizeof(struct window_control));

	const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	control->refresh_rate = 1000 / mode->refreshRate;
	control->rm = false; // Just in case

	glfwSetWindowUserPointer(window, control);
	glfwSetFramebufferSizeCallback(window, framebuffer_callback);
	glfwSetWindowSizeCallback(window, windowsize_callback);
	glfwSetWindowPosCallback(window, windowpos_callback);
	glfwSetKeyCallback(window, key_callback);
	glfwSwapInterval(1);
}

GLFWwindow * create_window() {
	if (glfwInit()) {
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#if GLFW_VERSION_MINOR >= 3
		glfwWindowHintString(GLFW_X11_CLASS_NAME, WU_CANON_NAME);
#endif

		GLFWwindow *window = glfwCreateWindow(640, 480, WU_CANON_NAME,
			NULL, NULL);
		glfwMakeContextCurrent(window);
		return window;
	}
	return NULL;
}
