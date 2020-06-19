#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <limits.h>

#include <epoxy/gl.h>
#include <GLFW/glfw3.h>

#include "wudefs.h"
#include "common.h"
#include "window.h"
#include "opengl.h"

static void toggle_fullscreen(GLFWwindow *window,
struct window_geometry *display) {
	if (display->fullscreen) {
		glfwSetWindowMonitor(window, NULL,
			display->window_x, display->window_y,
			display->window_w, display->window_h, GLFW_DONT_CARE);
	} else {
		GLFWmonitor *monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode *mode = glfwGetVideoMode(monitor);
		glfwSetWindowMonitor(window, monitor, 0, 0, mode->width,
			mode->height, mode->refreshRate);
	}
	display->fullscreen = !display->fullscreen;
}

static void focus_callback(GLFWwindow *window, int focused) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->display.has_focus = focused;
}

static void windowpos_callback(GLFWwindow *window, const int x, const int y) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	if (!control->display.fullscreen) {
		control->display.window_x = x;
		control->display.window_y = y;
	}
}

static void windowsize_callback(GLFWwindow *window, const int w, const int h) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	if (!control->display.fullscreen) {
		control->display.window_w = w;
		control->display.window_h = h;
	}
}

static void framebuffer_callback(GLFWwindow *window, const int w, const int h) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->context.fb_w = w;
	control->context.fb_h = h;
	even_gl_view(&control->context);
	control->state.fit_zoom = correct_gl_aspect_ratio(&control->context,
		control->state.rotate);
	update_gl_matrix(&control->context);

	control->conf.fb_w = (unsigned int)w;
	control->conf.fb_h = (unsigned int)h;
}

static void close_callback(GLFWwindow *window) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	control->event.program = close_window;
}

static void char_callback(GLFWwindow *window, unsigned int codepoint) {
	struct window_control *control = glfwGetWindowUserPointer(window);
	struct gl_context *context = &control->context;
	struct wu_state *state = &control->state;

	switch (codepoint) {
	case '+':
		set_gl_scaling(context, state, (float)(state->zoom * M_SQRT2));
		update_gl_matrix(context);
		return;
	case '-':
		set_gl_scaling(context, state, (float)(state->zoom * M_SQRT1_2));
		update_gl_matrix(context);
		return;
	}
}

static void key_callback(GLFWwindow *window, int key, int __scan, int action,
int mode) {
	(void)__scan;

	if (action != GLFW_RELEASE) {
		struct window_control *control =
			glfwGetWindowUserPointer(window);
		struct gl_context *context = &control->context;
		struct wu_state *state = &control->state;

		switch (key) {
		// All the different ways to exit.
		case GLFW_KEY_Q: case GLFW_KEY_ESCAPE:
			control->event.program = close_window;
			return;
		case GLFW_KEY_F4:
			if (mode & GLFW_MOD_ALT) {
				control->event.program = close_window;
			}
			return;
		case GLFW_KEY_W:
			if (mode & GLFW_MOD_CONTROL) {
				control->event.program = close_window;
			}
			return;

		// Fullscreen
		case GLFW_KEY_F: case GLFW_KEY_F11:
			toggle_fullscreen(window, &control->display);
			return;

		// Movement
		case GLFW_KEY_H: case GLFW_KEY_LEFT:
			change_gl_offset(context, true, MV_FACTOR, 0);
			update_gl_matrix(context);
			return;
		case GLFW_KEY_J: case GLFW_KEY_DOWN:
			change_gl_offset(context, true, 0, MV_FACTOR);
			update_gl_matrix(context);
			return;
		case GLFW_KEY_K: case GLFW_KEY_UP:
			change_gl_offset(context, true, 0, -MV_FACTOR);
			update_gl_matrix(context);
			return;
		case GLFW_KEY_L: case GLFW_KEY_RIGHT:
			change_gl_offset(context, true, -MV_FACTOR, 0);
			update_gl_matrix(context);
			return;

		// Zoom
		case GLFW_KEY_PAGE_UP:
			control->event.image = scale;
			set_gl_scaling(context, state,
				(float)(state->zoom * M_SQRT2));
			update_gl_matrix(context);
			return;
		case GLFW_KEY_PAGE_DOWN:
			control->event.image = scale;
			set_gl_scaling(context, state,
				(float)(state->zoom * M_SQRT1_2));
			update_gl_matrix(context);
			return;
		case GLFW_KEY_2:
			control->event.image = scale;
			set_gl_scaling(context, state, 2);
			update_gl_matrix(context);
			break;
		case GLFW_KEY_1: case GLFW_KEY_HOME:
			control->event.image = scale;
			set_gl_scaling(context, state, 1);
			update_gl_matrix(context);
			return;
		case GLFW_KEY_0: case GLFW_KEY_END:
			control->event.image = scale;
			change_gl_offset(context, false, 0, 0);
			set_gl_scaling(context, state, state->fit_zoom);
			update_gl_matrix(context);
			return;

		// Rotation. Positive is counter-clockwise
		case GLFW_KEY_Z:
			change_gl_rotation(context, state, 1);
			state->fit_zoom = correct_gl_aspect_ratio(context,
				state->rotate);
			update_gl_matrix(context);
			return;
		case GLFW_KEY_X:
			change_gl_rotation(context, state, -1);
			state->fit_zoom = correct_gl_aspect_ratio(context,
				state->rotate);
			update_gl_matrix(context);
			return;

		// Mirror
		case GLFW_KEY_I: // Horizontally
			state->mirror = !state->mirror;
			state->rotate = (unsigned char)iwrapadd(state->rotate, 2, 4);
			calc_gl_mirrot(context, state);
			correct_gl_aspect_ratio(context, state->rotate);
			update_gl_matrix(context);
			return;
		case GLFW_KEY_O: // Vertically
			state->mirror = !state->mirror;
			calc_gl_mirrot(context, state);
			correct_gl_aspect_ratio(context, state->rotate);
			update_gl_matrix(context);
			return;

		// Delete
		case GLFW_KEY_D:
		case GLFW_KEY_DELETE:
			if (!control->event.rm) {
				control->event.rm = warn_rm;
				print_temp_line("Do you wish to delete this "
					"file? (D to confirm, u to dismiss.)");
			} else if (mode & GLFW_MOD_SHIFT) {
				control->event.rm = yes_rm;
			}
			return;
		// Undo delete
		case GLFW_KEY_U:
			if (control->event.rm) {
				control->event.rm = no_rm;
				print_temp_line("Undone.");
			}
			return;

		// Change sub-image
		case GLFW_KEY_PERIOD:
			state->cycle_sub_img = 1;
			state->anim = paused;
			return;
		case GLFW_KEY_COMMA:
			state->cycle_sub_img = -1;
			state->anim = paused;
			return;
		case GLFW_KEY_SPACE:
			state->anim ^= 2;
			return;

		// Refresh file
		case GLFW_KEY_R: case GLFW_KEY_F5:
			control->event.program = reload_file;
			return;

		// Change file
		case GLFW_KEY_N:
			state->cycle = (mode & GLFW_MOD_SHIFT) ? 10 : 1;
			return;
		case GLFW_KEY_P:
			state->cycle = (mode & GLFW_MOD_SHIFT) ? -10 : -1;
			return;
		}
	}
}

void end_display(const struct window_control *control) {
	delete_gl_context(&control->context);
	glfwTerminate();
}

static bool update_window(struct window_control *control,
const struct raw_img *img, const bool new_image) {
	struct gl_context *context = &control->context;
	struct wu_state *state = &control->state;
	if (reuse_gl_texture(img, context)) {
		return true;
	} else if (load_gl_texture(img, context)) {
		if (new_image) {
			state->rotate = img->rotate;
			state->mirror = img->mirror;
		}

		calc_gl_mirrot(context, state);
		even_gl_view(context);
		state->fit_zoom = correct_gl_aspect_ratio(context, state->rotate);

		float zoom;
		if (new_image) {
			zoom = fminf(1, state->fit_zoom);
		} else {
			zoom = state->zoom;
		}
		set_gl_scaling(context, state, zoom);
		update_gl_matrix(context);
		return true;
	}
	return false;
}

static int idle_display(struct window_control *control, int remaining) {
	struct wu_event *event = &control->event;
	struct wu_state *state = &control->state;

	do {
		glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glfwSwapBuffers(control->window);
		if (state->anim == playing && control->display.has_focus) {
			glfwPollEvents();
			remaining -= control->display.refresh_rate;
			if (remaining < 0) {
				state->cycle_sub_img = 1;
			}
		} else {
			glfwWaitEvents();
		}
	} while (!state->cycle_sub_img && !state->cycle && !event->image
	&& !event->program && event->rm != yes_rm);

	if (state->cycle_sub_img) {
		event->image = sub_cycle;
	}
	return remaining;
}

bool window_loop(struct window_control *control, struct image_file *file,
const char *filename, const bool one_file_in_list) {
	clear_gl_color(control->conf.bg, file->bg, control->conf.use_img_bg);
	glfwSetWindowTitle(control->window, filename);

	struct raw_img *img = file->sub_img;
	int idx = 0;
	int remaining = img[idx].msec;
	bool first_load = true;
	bool all_ok = true;

	struct wu_state *state = &control->state;
	state->anim = file->is_animation ? playing : none;

	struct timespec before, after;
	do {
		if (first_load || state->cycle_sub_img) {
			state->cycle_sub_img = 0;

			if (state->anim != playing) {
				clock_gettime(CLOCK_REALTIME, &before);
			}

			if (!update_window(control, img + idx, true)) {
				printf("Failed to load %s to texture.\n",
					filename);
				all_ok = false;
				break;
			}
			first_load = false;

			if (state->anim != playing) {
				clock_gettime(CLOCK_REALTIME, &after);
				printf("Sub-image %d uploaded in %ld "
					"nanoseconds.\n", idx,
					timespec_nanodiff(&before, &after));
			}

			if (file->nr == 1 && img[idx].data && !file->callback) {
				free(img[idx].data);
				img[idx].data = NULL;
			}
		}

		if (state->anim == playing) {
			remaining += imax(img[idx].msec,
				control->display.refresh_rate);
		}

		remaining = idle_display(control, remaining);

		if (control->event.program) {
			break;
		} else if (file->callback && (file->events & control->event.image)) {
			const enum wu_error err = file->callback(file,
				&control->conf, state, control->event.image);
			if (err != wu_ok) {
				printf("Callback failed with code %d: %s\n",
					err, wu_error_message(err));
			} else if (!update_window(control, img + idx, false)) {
				++state->cycle;
			}
		}
		control->event.image = 0;
		idx = iwrapadd(idx, state->cycle_sub_img, (int)file->nr);

	} while ((!state->cycle || one_file_in_list)
	&& control->event.rm != yes_rm);

	if (file->callback) {
		file->callback(file, &control->conf, state, 0);
	}
	return all_ok;
}

bool setup_display(struct window_control *control) {
	if (!glfwInit()) {
		puts("Failed to create window.");
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	if (control->conf.no_window_decorations) {
		glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	}
	if (control->conf.bg[3] < UCHAR_MAX) {
		glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
	}
#if GLFW_VERSION_MINOR >= 3
	glfwWindowHintString(GLFW_X11_CLASS_NAME, WU_CANON_NAME);
#endif

	if (!control->conf.initial_w) {
		control->conf.initial_w = 640;
	}
	if (!control->conf.initial_h) {
		control->conf.initial_h = 480;
	}
	GLFWwindow *window = glfwCreateWindow(control->conf.initial_w,
		control->conf.initial_h, WU_CANON_NAME, NULL, NULL);
	glfwMakeContextCurrent(window);

	if (!setup_opengl(&control->context, &control->conf)) {
		puts("Failed to setup OpenGL context.");
		glfwTerminate();
		return false;
	}

	glfwSetWindowUserPointer(window, control);
	glfwSetWindowCloseCallback(window, close_callback);
	glfwSetWindowFocusCallback(window, focus_callback);
	glfwSetFramebufferSizeCallback(window, framebuffer_callback);
	glfwSetWindowSizeCallback(window, windowsize_callback);
	glfwSetWindowPosCallback(window, windowpos_callback);
	glfwSetKeyCallback(window, key_callback);
	glfwSetCharCallback(window, char_callback);
	glfwSwapInterval(1);
	control->window = window;

	const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	struct window_geometry *display = &control->display;
	display->monitor_w = mode->width;
	display->monitor_h = mode->height;
	display->refresh_rate = 1000 / mode->refreshRate;
	display->has_focus = true;
	display->fullscreen = false;

	memset(&control->state, 0, sizeof(control->state));
	memset(&control->event, 0, sizeof(control->event));

	// In case the window manager overrode our window size.
	glfwPollEvents();
	return true;
}
