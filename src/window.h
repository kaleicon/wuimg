#ifndef WU_WINDOW
#define WU_WINDOW

#include "wudefs.h"
#include "opengl.h"
#include "events.h"

#include "window/glfw.h"
#include "window/drm.h"
#include "window/wayland.h"

enum window_backend {
	window_glfw,
	window_drm,
	window_wayland,
};

struct window_context {
	struct window_public pub;
	enum window_backend backend:8;
	union {
		struct glfw_context glfw;
		struct drm_context drm;
		struct wayland wl;
	} ctx;
};

void window_terminate(struct window_context *window);

void window_draw(struct window_context *window);

double window_poll(struct window_context *window);

bool window_has_focus(const struct window_context *window);

void window_set_title(const struct window_context *window, const char *title);

void window_postgl_setup(struct window_context *window);

bool window_setup(struct window_context *window);

#endif /* WU_WINDOW */
