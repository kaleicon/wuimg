#ifndef WINDOW
#define WINDOW

#include "wudefs.h"
#include "opengl.h"
#include "events.h"
#include "drm.h"
#

#include <GLFW/glfw3.h>

enum window_backend {
	window_glfw,
	window_drm,
};

struct glfw_context {
	GLFWwindow *window;
	struct window_geom { // Fullscreen restoration
		int x, y, w, h;
	} geom;
	struct window_cursor {
		float x, y;
		int pressed;
	} cursor;

	bool fullscreen;
	bool has_focus;
};

struct window_context {
	struct gl_context gl;
	struct wu_event event;
	enum window_backend backend:8;
	union {
		struct glfw_context glfw;
		struct drm_context drm;
	} ctx;
};

struct window_control {
	struct window_context window;
	struct image_context image;
};

void window_terminate(struct window_context *window);

void window_event(struct window_context *window);

void window_poll(const struct window_context *window);

void window_draw(struct window_context *window);

bool window_has_focus(const struct window_context *window);

void window_set_title(const struct window_context *window, const char *title);

void window_postgl_setup(struct window_control *control);

bool window_setup(struct window_control *control);

#endif /* WINDOW */
