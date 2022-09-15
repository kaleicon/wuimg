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

const char * window_backend_name(const enum window_backend backend) {
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

bool window_draw(struct window_context *window) {
	if (gl_draw(&window->pub.gl, &window->pub.image.state)) {
		switch (window->backend) {
		case window_glfw: glfwSwapBuffers(window->ctx.glfw.window); break;
		case window_drm: drm_swap_buffers(&window->ctx.drm); break;
		case window_wayland: egl_swap(&window->pub.win.egl); break;
		case window_egl: break;
		}
		return true;
	}
	return false;
}

void window_fullscreen(struct window_context *window) {
	const bool fs = window->pub.win.fullscreen;
	switch (window->backend) {
	case window_glfw:
		glfw_fullscreen(&window->ctx.glfw, fs);
		break;
	case window_wayland:
		wayland_fullscreen(&window->ctx.wl, fs);
		break;
	case window_drm:
	case window_egl:
		break;
	}
	window->pub.win.fullscreen = !fs;
}

void window_poll(struct window_context *window) {
	switch (window->backend) {
	case window_glfw:
		glfwPollEvents();
		break;
	case window_wayland:
		wayland_poll(&window->ctx.wl, 0);
		break;
	case window_drm:
	case window_egl:
		break;
	}

	if (sig_should_close) {
		window->pub.event.program = wu_program_exit;
	}
}

void window_adapt(struct window_context *window) {
	const struct gl_image_info *tex = &window->pub.gl.tex;
	int w = (int)tex->w;
	int h = (int)tex->h;
	switch (window->backend) {
	case window_glfw:
		glfwSetWindowSize(window->ctx.glfw.window, w, h);
		break;
	case window_wayland:
		wayland_resize(&window->ctx.wl, w, h);
		break;
	case window_drm:
	case window_egl:
		break;
	}
}

bool window_has_focus(const struct window_context *window) {
	return window->pub.win.focused;
}

void window_set_title(const struct window_context *window, const char *title) {
	switch (window->backend) {
	case window_glfw:
		glfwSetWindowTitle(window->ctx.glfw.window, title);
		break;
	case window_wayland:
		wayland_set_title(&window->ctx.wl, title);
		break;
	case window_drm:
	case window_egl:
		break;
	}
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

	switch (window->backend) {
	case window_glfw:
	case window_wayland:
		window_poll(window);
		break;
	case window_drm:
	case window_egl:
		break;
	}
	window->pub.win.focused = true;
}

static void error_cleanup(struct window_context *window, const char *err) {
	window_terminate(window);
	term_line_key_val(window_backend_name(window->backend), err, stderr);
}

bool window_setup(struct window_context *window) {
	struct wu_conf *conf = &window->pub.image.conf;
	if (!conf->initial_size.w || !conf->initial_size.h) {
		conf->initial_size.w = 640;
		conf->initial_size.h = 480;
	}

	const char *err = NULL;
	const bool wayland = (bool)getenv("WAYLAND_DISPLAY");
	if (wayland && !getenv("WU_GLFW")) {
		window->backend = window_wayland;
		err = wayland_init(&window->ctx.wl, &window->pub);
		if (!err) {
			return true;
		}
		error_cleanup(window, err);
	}

	if (wayland || getenv("DISPLAY") /* Xorg */) {
		window->backend = window_glfw;
		err = glfw_setup(&window->ctx.glfw, &window->pub);
	} else {
		window->backend = window_drm;
		err = drm_init(&window->ctx.drm, &window->pub);
	}

	if (err) {
		error_cleanup(window, err);
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
		term_line_put(err, stderr);
		return false;
	}
	return true;
}
