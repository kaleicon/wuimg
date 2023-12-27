// SPDX-License-Identifier: 0BSD
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "term.h"
#include "window.h"

static volatile sig_atomic_t sig_should_close = 0;

static void signal_handler(int _signum) {
	(void)_signum;
	sig_should_close = 1;
}

void window_terminate(struct window_context *window) {
	window->pub.win.fn.terminate(&window->ctx);
}

bool window_draw(struct window_context *window) {
	if (gl_draw(&window->pub.gl, &window->pub.image.state)) {
		window->pub.win.fn.swap_buffers(&window->ctx);
		glFinish();
		return true;
	}
	return false;
}

void window_fullscreen(struct window_context *window) {
	const bool fs = window->pub.win.fullscreen;
	window->pub.win.fn.fullscreen(&window->ctx, fs);
	window->pub.win.fullscreen = !fs;
}

void window_poll(struct window_context *window, const long nsecs) {
	window->pub.win.fn.poll(&window->ctx, nsecs);
	if (sig_should_close) {
		window->pub.event.program = wu_program_exit;
	}
}

void window_adapt(struct window_context *window) {
	const struct gl_context *context = &window->pub.gl;
	const int w = (int)context->tex.w;
	const int h = (int)context->tex.h;
	window->pub.win.fn.resize(&window->ctx, w, h);
}

void window_set_title(struct window_context *window, const char *title) {
	window->pub.win.fn.title(&window->ctx, title);
}

void window_postgl_setup(struct window_context *window) {
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

	window_poll(window, 0);
	window->pub.win.focused = true;
}

static void error_cleanup(struct window_context *window, const char *err) {
	if (window->backend) {
		window_terminate(window);
		term_line_key_val(window->backend, err, stderr);
	} else {
		term_line_key_val("Window", err, stderr);
	}
}

bool window_setup(struct window_context *window) {
	const char *err = "No display available";
	const bool wayland = getenv("WAYLAND_DISPLAY");
	if (wayland && !getenv("WU_GLFW")) {
#ifdef WU_ENABLE_WINDOW_WAYLAND
		window->backend = "Wayland";
		err = wayland_init(&window->ctx.wl, &window->pub);
		if (!err) {
			return true;
		}
		error_cleanup(window, err);
#endif
	}

	if (wayland || getenv("DISPLAY") /* X11 */) {
#ifdef WU_ENABLE_WINDOW_GLFW
		window->backend = "GLFW";
		err = glfw_setup(&window->ctx.glfw, &window->pub);
#endif
	} else {
#ifdef WU_ENABLE_WINDOW_DRM
		window->backend = "DRM";
		err = drm_init(&window->ctx.drm, &window->pub);
#endif
	}

	if (err) {
		error_cleanup(window, err);
		return false;
	}
	return true;
}

void window_offscreen_terminate(struct window_offscreen *window) {
	window->terminate(&window->ctx);
}

bool window_offscreen_setup(struct window_offscreen *window) {
	window_fn_ctx_t *terminate = &window->terminate;
	if (egl_offscreen_init(&window->ctx.egl, terminate, EGL_DEFAULT_DISPLAY)
	== NULL) {
		window->backend = "EGL offscreen";
		return true;
	}

	const char *err = "No offscreen display available";
	if (getenv("WAYLAND_DISPLAY")) {
#ifdef WU_ENABLE_WINDOW_WAYLAND
		window->backend = "Wayland offscreen";
		err = wayland_offscreen_init(&window->ctx.wl, terminate);
#endif
	} else if (!getenv("DISPLAY")) {
#ifdef WU_ENABLE_WINDOW_DRM
		window->backend = "DRM offscreen";
		err = drm_offscreen_init(&window->ctx.drm, terminate);
#endif
	}

	if (err) {
		term_line_put(err, stderr);
		return false;
	}
	return true;
}
