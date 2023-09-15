// SPDX-License-Identifier: 0BSD
#ifndef WU_WAYLAND
#define WU_WAYLAND

#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client-header.h"
#include <xkbcommon/xkbcommon.h>

#include "conf.h"
#include "window/base.h"
#include "window/egl.h"

struct wayland_binds {
	struct wl_compositor *comp;
	struct wl_seat *seat;
	struct wl_shm *shm;
	struct xdg_wm_base *xwb;
};

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

struct wayland {
	struct window_public *pub;

	struct wl_display *display;
	struct wl_registry *reg;

	struct wayland_binds binds;

	struct wayland_cursor cursor;
	struct wayland_keyboard kb;

	struct wl_surface *surf;
	struct wl_egl_window *egl_window;
	struct xdg_surface *xdg_surf;
	struct xdg_toplevel *toplevel;

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
