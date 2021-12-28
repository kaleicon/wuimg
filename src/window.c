#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"
#include "events.h"
#include "window.h"

void window_terminate(struct window_context *window) {
	switch (window->backend) {
	case window_glfw:
		glfwTerminate();
		break;
	case window_drm:
		drm_terminate(&window->ctx.drm);
		break;
	case window_wayland:
		wayland_terminate(&window->ctx.wl);
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
	case window_wayland:
		wayland_swap_buffers(&window->ctx.wl);
		break;
	}
}

static void handle_window_event(struct window_context *window) {
	switch (window->pub.event.window) {
	case toggle_fullscreen:
		switch (window->backend) {
		case window_glfw:
			glfw_toggle_fullscreen(&window->ctx.glfw);
			break;
		case window_drm:
			break;
		case window_wayland:
			wayland_fullscreen(&window->ctx.wl);
			break;
		}
		break;
	case toggle_alpha:
		gl_alpha_toggle(&window->pub.gl);
		break;
	}
	window->pub.event.window = 0;
}

double window_poll(struct window_context *window) {
	struct window_public *pub = &window->pub;
	struct window_cursor *cursor = NULL;
	switch (window->backend) {
	case window_glfw:
		glfwPollEvents();
		cursor = &window->ctx.glfw.cursor;
		break;
	case window_drm:
		break;
	case window_wayland:
		wayland_poll(&window->ctx.wl);
		cursor = &window->ctx.wl.cursor.state;
		break;
	}

	if (cursor) {
		pub->image.state.cycle = iclamp((int)cursor->x.scroll, -1, 1);
		pub->event.cycle = iclamp((int)cursor->y.scroll, -1, 1);
		cursor->x.scroll = 0;
		cursor->y.scroll = 0;
	}

	const double ellapsed = window_exec_events(&window->pub);
	handle_window_event(window);
	return ellapsed;
}

bool window_has_focus(const struct window_context *window) {
	switch (window->backend) {
	case window_glfw:
		return window->ctx.glfw.has_focus;
	case window_drm:
		break;
	case window_wayland:
		return window->ctx.wl.active;
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
	case window_wayland:
		wayland_set_title(&window->ctx.wl, title);
		break;
	}
}

void window_postgl_setup(struct window_context *window) {
	switch (window->backend) {
	case window_glfw:
		glfwPollEvents();
		break;
	case window_drm:
		break;
	case window_wayland:
		wl_display_dispatch(window->ctx.wl.display);
		break;
	}
	window->pub.image.state.zoom = 1;
	window->pub.image.state.fit_zoom = 1;
}

static void init_error_handle(struct window_context *window, const char *name,
const char *err) {
	window_terminate(window);
	fprintf(stderr, "%s: %s\n", name, err);
}

bool window_setup(struct window_context *window) {
	const char *err = NULL;
	const bool wayland = (bool)getenv("WAYLAND_DISPLAY");
	if (wayland && !getenv("WU_GLFW")) {
		window->backend = window_wayland;
		err = wayland_init(&window->ctx.wl, &window->pub);
		if (!err) {
			return true;
		}
		init_error_handle(window, "Wayland", err);
		memset(&window->ctx, 0, sizeof(window->ctx));
	}

	const char *name = NULL;
	if (wayland || getenv("DISPLAY") /* Xorg */) {
		window->backend = window_glfw;
		name = "GLFW";
		err = glfw_setup(&window->ctx.glfw, &window->pub);
	} else {
		window->backend = window_drm;
		name = "DRM";
		err = drm_init(&window->ctx.drm, &window->pub);
	}

	if (err) {
		init_error_handle(window, name, err);
		return false;
	}
	return true;
}
