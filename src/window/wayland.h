#ifndef WU_WAYLAND
#define WU_WAYLAND

#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client-protocol.h"
#include <xkbcommon/xkbcommon.h>

#include "../conf.h"
#include "egl.h"
#include "base.h"

struct wayland_listeners {
	struct wl_registry_listener reg;
	struct wl_seat_listener seat;
	struct wl_pointer_listener pointer;
	struct wl_keyboard_listener keyboard;
	struct xdg_wm_base_listener wm_base;
	struct xdg_surface_listener surface;
	struct xdg_toplevel_listener toplevel;
};

struct wayland_binds {
	struct wl_compositor *comp;
	struct wl_seat *seat;
	struct wl_shm *shm;
	struct xdg_wm_base *xwb;
};

struct wayland_cursor {
	struct wl_surface *surf;
	struct wl_buffer *buf;
	struct window_cursor state;
};

struct xkb {
	struct xkb_context *ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
};

struct wayland {
	struct window_public *pub;

	struct wl_display *display;
	struct wl_registry *reg;

	struct wayland_listeners listen;
	struct wayland_binds binds;

	struct wl_pointer *pointer;
	struct wayland_cursor cursor;
	struct xkb xkb;
	struct wl_keyboard *keyboard;

	struct wl_surface *surf;
	struct xdg_surface *xdg_surf;
	struct xdg_toplevel *toplevel;

	struct wl_egl_window *egl_window;
	struct egl egl;

	bool active;
	bool fullscreen;
};

struct wayland_offscreen {
	struct wl_display *display;
	EGLDisplay egl_display;
};

void wayland_terminate(struct wayland *wl);

void wayland_set_title(const struct wayland *wl, const char *title);

bool wayland_swap_buffers(const struct wayland *wl);

void wayland_poll(struct wayland *wl);

void wayland_fullscreen(struct wayland *wl);

const char * wayland_init(struct wayland *wl, struct window_public *pub);


void wayland_offscreen_terminate(struct wayland_offscreen *wl);

const char * wayland_offscreen_init(struct wayland_offscreen *wl);

#endif /* WU_WAYLAND */
