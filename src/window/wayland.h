// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef WU_WAYLAND
#define WU_WAYLAND

#include <wayland-client.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>

#include "xdg-shell-client-header.h"
#include "color-management-client-header.h"
#include "content-type-client-header.h"
#include "cursor-shape-client-header.h"
#include "xdg-decoration-client-header.h"

#include "conf.h"
#include "window/base.h"
#include "window/egl.h"

struct wayland_cursor {
	struct wl_pointer *pointer;
	struct wl_surface *surf;
	struct wl_buffer *buf;
};

struct wayland_keyboard {
	struct xkb_context *ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
	struct wl_keyboard *keyboard;
};

struct wayland_color {
	struct wp_color_manager_v1 *bind;
	struct wp_color_management_surface_v1 *surf;
	struct wp_color_management_surface_feedback_v1 *feedback;
};

union wayland_content {
	struct wp_content_type_manager_v1 *bind;
	struct wp_content_type_v1 *type;
};

union wayland_decoration {
	struct zxdg_decoration_manager_v1 *bind;
	struct zxdg_toplevel_decoration_v1 *toplevel;
};

struct wayland {
	struct window_public *pub;

	struct wl_display *display;
	struct wl_registry *reg;

	struct wl_compositor *comp;
	struct wl_seat *seat;
	struct wl_shm *shm;
	struct xdg_wm_base *xwb;
	struct wp_cursor_shape_manager_v1 *shape;

	struct wayland_cursor cursor;
	struct wayland_keyboard kb;

	struct wl_surface *surf;
	struct wl_egl_window *egl_window;
	struct xdg_surface *xdg_surf;
	struct xdg_toplevel *toplevel;

	struct wayland_color color;
	union wayland_content content;
	union wayland_decoration deco;

	struct egl egl;
};

struct wayland_offscreen {
	struct wl_display *display;
	EGLDisplay egl_display;
};

const char * wayland_init(struct wayland *wl, struct window_public *pub);

const char * wayland_offscreen_init(struct wayland_offscreen *wl,
window_fn_ctx_t *terminate);

#endif /* WU_WAYLAND */
