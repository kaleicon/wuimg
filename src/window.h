// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#ifndef WU_WINDOW
#define WU_WINDOW

#include "window_enable.def"
#include "window/egl.h"

#ifdef WU_ENABLE_WINDOW_GLFW
#include "window/glfw.h"
#endif
#ifdef WU_ENABLE_WINDOW_DRM
#include "window/drm.h"
#endif
#ifdef WU_ENABLE_WINDOW_WAYLAND
#include "window/wayland.h"
#endif

struct window_context {
	struct window_public pub;
	union {
		bool _non_empty_union;
#ifdef WU_ENABLE_WINDOW_GLFW
		struct glfw_context glfw;
#endif
#ifdef WU_ENABLE_WINDOW_DRM
		struct drm_context drm;
#endif
#ifdef WU_ENABLE_WINDOW_WAYLAND
		struct wayland wl;
#endif
	} ctx;
	const char *backend;
};

struct window_offscreen {
	window_fn_ctx_t terminate;
	union {
		EGLDisplay egl;
#ifdef WU_ENABLE_WINDOW_DRM
		struct drm_offscreen drm;
#endif
#ifdef WU_ENABLE_WINDOW_WAYLAND
		struct wayland_offscreen wl;
#endif
	} ctx;
	const char *backend;
};

void window_terminate(struct window_context *window);

bool window_draw(struct window_context *window);

void window_fullscreen(struct window_context *window);

void window_poll(struct window_context *window, long nsecs);

void window_adapt(struct window_context *window);

void window_set_title(struct window_context *window, const char *title);

void window_postgl_setup(struct window_context *window);

bool window_setup(struct window_context *window);


void window_offscreen_terminate(struct window_offscreen *window);

bool window_offscreen_setup(struct window_offscreen *window);

#endif /* WU_WINDOW */
