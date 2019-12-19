#include <stdio.h>

#include <epoxy/gl.h>
#include <GLFW/glfw3.h>

#include "wudefs.h"
#include "common.h"
#include "window.h"
#include "opengl.h"

static void set_zoom(struct gl_context *context, float zoom) {
	context->zoom = fclampf(zoom, MIN_ZOOM, MAX_ZOOM);
	update_gl_scaling(context);
}

static void reset_offset(struct gl_context *context) {
	context->offset_x = 0;
	context->offset_y = 0;
	glUniform2f(context->offset_uni, context->offset_x, context->offset_y);
}

static void change_offset(float x, float y, struct gl_context *context) {
	context->offset_x += x / context->normal_x / context->zoom;
	context->offset_y += y / context->normal_y / context->zoom;
	glUniform2f(context->offset_uni, context->offset_x, context->offset_y);
}

static void framebuffer_callback(GLFWwindow *window, int width, int height) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	normalize_gl_viewport(control->context, width, height);
	update_gl_scaling(control->context);
}

static void key_callback(GLFWwindow *window, int key,
__attribute__((unused))int scan, int action, __attribute__((unused))int mode) {
	switch (key) {
	case GLFW_KEY_ESCAPE:
	case GLFW_KEY_Q:
		glfwSetWindowShouldClose(window, GLFW_TRUE);
		return;
	}

	struct window_control *control = glfwGetWindowUserPointer(window);
	struct gl_context *context = control->context;
	if (action != GLFW_RELEASE) {
		switch (key) {
		// Movement
		case GLFW_KEY_LEFT: case GLFW_KEY_H:
			change_offset(MV_FACTOR, 0, context);
			return;
		case GLFW_KEY_RIGHT: case GLFW_KEY_L:
			change_offset(-MV_FACTOR, 0, context);
			return;
		case GLFW_KEY_DOWN: case GLFW_KEY_J:
			change_offset(0, MV_FACTOR, context);
			return;
		case GLFW_KEY_UP: case GLFW_KEY_K:
			change_offset(0, -MV_FACTOR, context);
			return;
		// Zoom
		case GLFW_KEY_PAGE_UP:
			set_zoom(context, context->zoom * 2);
			return;
		case GLFW_KEY_PAGE_DOWN:
			set_zoom(context, context->zoom * (float)0.5);
			return;
		case GLFW_KEY_HOME:
			set_zoom(context, 1);
			return;
		case GLFW_KEY_END:
			reset_offset(context);
			set_zoom(context, context->fit_zoom);
			return;
		// Change file
		case GLFW_KEY_N:
			++control->cycle;
			break;
		case GLFW_KEY_P:
			--control->cycle;
			break;
		// Change sub-image
		case GLFW_KEY_PERIOD:
			++control->cycle_sub_img;
			control->anim = paused;
			break;
		case GLFW_KEY_COMMA:
			--control->cycle_sub_img;
			control->anim = paused;
			break;
		case GLFW_KEY_SPACE:
			control->anim ^= 2;
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

int get_monitor_refresh_rate() {
	const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	return mode->refreshRate;
}

bool update_window(GLFWwindow *window, struct window_control *control,
const struct image_file *file, const int idx) {
	control->cycle = 0;
	control->cycle_sub_img = 0;
	if (idx == 0) {
		if (file->is_animation && file->nr > 1) {
			control->anim = playing;
		}
		glfwSetWindowTitle(window, file->name);
	}

	struct gl_context *context = control->context;
	struct raw_img *img = file->sub_img;
	if (update_gl_texture(&img[idx], context)) {
		return true;
	} else if (load_gl_texture(&img[idx], context)) {
		reset_gl_draw_state(context);

		int fb_w, fb_h;
		glfwGetFramebufferSize(window, &fb_w, &fb_h);
		normalize_gl_viewport(context, fb_w, fb_h);
		context->zoom = fminf(1, context->fit_zoom);
		update_gl_scaling(context);
		return true;
	}
	return false;
}

void setup_window(GLFWwindow *window, struct window_control *control) {
	control->cycle = 0;
	control->cycle_sub_img = 0;
	control->cycle_wait = 0;

	glfwSetWindowUserPointer(window, control);
	glfwSetKeyCallback(window, key_callback);
	glfwSetFramebufferSizeCallback(window, framebuffer_callback);
	glfwSwapInterval(1);
}

GLFWwindow * create_window() {
	if (glfwInit()) {
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
//		glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
//		glfwWindowHintString(GLFW_X11_CLASS_NAME, WU_CANON_NAME);

		GLFWwindow *window = glfwCreateWindow(640, 480, WU_CANON_NAME,
			NULL, NULL);
		glfwMakeContextCurrent(window);
		return window;
	}
	return NULL;
}
