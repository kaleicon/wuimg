#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <signal.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"
#include "events.h"
#include "window.h"

static volatile sig_atomic_t sig_should_close = 0;

static void signal_handler(int _signum) {
	(void)_signum;
	sig_should_close = 1;
}

const char * window_backend_str(const enum window_backend backend) {
	switch (backend) {
	case window_glfw: return "GLFW";
	case window_drm: return "DRM";
	case window_wayland: return "Wayland";
	case window_egl: return "EGL";
	}
	return "???";
}

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
	case window_egl:
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
	case window_egl:
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
		case window_egl:
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
	case window_egl:
		break;
	}

	if (cursor) {
		pub->event.image = image_sub_cycle(&pub->image,
			iclamp((int)cursor->x.scroll, -1, 1));
		pub->event.cycle = iclamp((int)cursor->y.scroll, -1, 1);
		cursor->x.scroll = 0;
		cursor->y.scroll = 0;
	}

	const double ellapsed = window_exec_events(pub);
	handle_window_event(window);
	if (sig_should_close) {
		window->pub.event.program = close_window;
	}
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
	case window_egl:
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
	case window_wayland:
		wayland_set_title(&window->ctx.wl, title);
		break;
	case window_egl:
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
	case window_egl:
		break;
	}
	window->pub.image.state.zoom = 1;
	window->pub.image.state.fit_zoom = 1;

	const struct sigaction act = {
		.sa_handler = signal_handler,
		.sa_flags = (int)SA_RESETHAND,
	};
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGTERM, &act, NULL);

	const struct sigaction ign = {
		.sa_handler = SIG_IGN,
	};
	sigaction(SIGHUP, &ign, NULL);
	sigaction(SIGPIPE, &ign, NULL);
}

static void init_error_handle(struct window_context *window, const char *err) {
	window_terminate(window);
	fprintf(stderr, "%s: %s\n", window_backend_str(window->backend), err);
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
		init_error_handle(window, err);
	}

	if (wayland || getenv("DISPLAY") /* Xorg */) {
		window->backend = window_glfw;
		err = glfw_setup(&window->ctx.glfw, &window->pub);
	} else {
		window->backend = window_drm;
		err = drm_init(&window->ctx.drm, &window->pub);
	}

	if (err) {
		init_error_handle(window, err);
		return false;
	}
	return true;
}

void window_offscreen_terminate(struct window_offscreen *window) {
	switch (window->backend) {
	case window_glfw:
		break;
	case window_drm:
		drm_offscreen_terminate(&window->ctx.drm);
		break;
	case window_wayland:
		wayland_offscreen_terminate(&window->ctx.wl);
		break;
	case window_egl:
		eglTerminate(window->ctx.egl);
		break;
	}
}

bool window_offscreen_setup(struct window_offscreen *window) {
	if (egl_offscreen_init(&window->ctx.egl, EGL_DEFAULT_DISPLAY) == NULL) {
		window->backend = window_egl;
		return true;
	}

	const char *err = "No offscreen display available";
	if (getenv("WAYLAND_DISPLAY")) {
		window->backend = window_wayland;
		err = wayland_offscreen_init(&window->ctx.wl);
	} else if (!getenv("DISPLAY")) {
		window->backend = window_drm;
		err = drm_offscreen_init(&window->ctx.drm);
	}

	if (err) {
		fprintf(stderr, "%s\n", err);
		return false;
	}
	return true;
}
