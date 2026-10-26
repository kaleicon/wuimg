// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "misc/term.h"
#include "misc/utf8.h"
#include "window.h"

#include "window_enable.def"
#include "window/egl.h"
#ifdef WU_ENABLE_WINDOW_DRM
#include "window/drm.h"
#endif
#ifdef WU_ENABLE_WINDOW_WAYLAND
#include "window/wayland.h"
#endif
#ifdef WU_ENABLE_WINDOW_GLFW
#include "window/glfw.h"
#endif

const char WINDOW_ENV_VARIABLES[] =
	"Window env variables:\n"
	"\tWU_GLFW\n"
	"\t\tForce GLFW backend, even if the Wayland one is available.\n";

static volatile sig_atomic_t sig_should_close = 0;

static void signal_handler(int _signum) {
	(void)_signum;
	sig_should_close = 1;
}

void window_terminate(struct window_context *window) {
	window->win.fn.terminate(window);
}

bool window_draw(struct window_context *window) {
	if (gl_draw(&window->gl, &window->image.state)) {
		window->win.fn.swap_buffers(window);
		glFinish();
		return true;
	}
	return false;
}

void window_fullscreen(struct window_context *window) {
	const bool fs = window->win.fullscreen;
	window->win.fn.fullscreen(window, fs);
	window->win.fullscreen = !fs;
}

void window_poll(struct window_context *window, const int msecs) {
	window->win.fn.poll(window, msecs);
	window->event.exit |= sig_should_close;
}

void window_adapt(struct window_context *window) {
	const struct gl_context *gl = &window->gl;
	const int w = (int)gl->tex.w;
	const int h = (int)gl->tex.h;
	window->win.fn.resize(window, w, h);
}

void window_set_title(struct window_context *window, const char *title) {
	const char *put = "";
	if (!title) {
		window->win.fn.title(window, put);
		return;
	}
	struct wustr conv;
	switch (utf8_convert(title, strlen(title), &conv, NULL)) {
	case trit_false: put = title; break;
	case trit_true: put = (const char *)conv.str; break;
	case trit_what: break;
	}
	window->win.fn.title(window, put);
	wustr_free(&conv);
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
	window->win.focused = true;
}

static void error_log(const char *where, const char *fallback, const char *err) {
	term_line_key_val(where ? where : fallback, err, stderr);
}

static void error_cleanup(struct window_context *window, const char *err) {
	if (window->win.ctx) {
		window_terminate(window);
	}
	error_log(window->win.backend_name, "Window", err);
	memset(&window->win, 0, sizeof(window->win));
}

bool window_setup(struct window_context *window) {
	const char *err = "No display available";
	const bool wayland = getenv("WAYLAND_DISPLAY");
	if (wayland && !getenv("WU_GLFW")) {
#ifdef WU_ENABLE_WINDOW_WAYLAND
		err = wayland_init(window);
		if (!err) {
			return true;
		}
		error_cleanup(window, err);
#endif
	}

	if (wayland || getenv("DISPLAY") /* X11 */) {
#ifdef WU_ENABLE_WINDOW_GLFW
		err = glfw_setup(window);
#endif
	} else {
#ifdef WU_ENABLE_WINDOW_DRM
		err = drm_init(window);
#endif
	}

	if (err) {
		error_cleanup(window, err);
		return false;
	}
	return true;
}

void window_offscreen_terminate(struct window_offscreen *window) {
	window->terminate(window);
}

static void offscreen_error_cleanup(struct window_offscreen *window,
const char *err) {
	if (window->terminate) {
		window_offscreen_terminate(window);
	}
	error_log(window->backend_name, "Offscreen context", err);
	memset(window, 0, sizeof(*window));
}

bool window_offscreen_setup(struct window_offscreen *window) {
	const char *err = egl_offscreen_init(window, EGL_DEFAULT_DISPLAY);
	if (!err) {
		return true;
	}
	offscreen_error_cleanup(window, err);

	if (getenv("WAYLAND_DISPLAY")) {
#ifdef WU_ENABLE_WINDOW_WAYLAND
		err = wayland_offscreen_init(window);
#endif
	} else if (!getenv("DISPLAY")) {
#ifdef WU_ENABLE_WINDOW_DRM
		err = drm_offscreen_init(window);
#endif
	}

	if (err) {
		offscreen_error_cleanup(window, err);
		return false;
	}
	return true;
}
