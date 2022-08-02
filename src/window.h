#ifndef WU_WINDOW
#define WU_WINDOW

#include "window/glfw.h"
#include "window/drm.h"
#include "window/wayland.h"
#include "window/egl.h"

enum window_backend {
	window_glfw = 1,
	window_drm,
	window_wayland,
	window_egl,
};

struct window_context {
	struct window_public pub;
	enum window_backend backend;
	union {
		struct glfw_context glfw;
		struct drm_context drm;
		struct wayland wl;
	} ctx;
};

struct window_offscreen {
	enum window_backend backend;
	union {
		struct drm_offscreen drm;
		struct wayland_offscreen wl;
		EGLDisplay egl;
	} ctx;
};

const char * window_backend_name(enum window_backend backend);

void window_terminate(struct window_context *window);

bool window_draw(struct window_context *window);

void window_fullscreen(struct window_context *window);

void window_poll(struct window_context *window);

void window_adapt(struct window_context *window);

bool window_has_focus(const struct window_context *window);

void window_set_title(const struct window_context *window, const char *title);

void window_postgl_setup(struct window_context *window);

bool window_setup(struct window_context *window);


void window_offscreen_terminate(struct window_offscreen *window);

bool window_offscreen_setup(struct window_offscreen *window);

#endif /* WU_WINDOW */
