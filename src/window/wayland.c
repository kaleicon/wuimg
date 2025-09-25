// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>

#include <linux/input-event-codes.h>

#include "misc/math.h"
#include "window/wayland.h"

static void reset_xkb(struct wayland_keyboard *k) {
	if (k->state) {
		xkb_state_unref(k->state);
		k->state = NULL;
	}
	if (k->keymap) {
		xkb_keymap_unref(k->keymap);
		k->keymap = NULL;
	}
}

static void destroy_keyboard(struct wayland_keyboard *k) {
	if (k->keyboard) {
		wl_keyboard_destroy(k->keyboard);
	}
	reset_xkb(k);
	if (k->ctx) {
		xkb_context_unref(k->ctx);
	}
}

static void destroy_cursor(struct wayland_cursor *c) {
	if (c->surf) {
		wl_surface_destroy(c->surf);
	}
	if (c->buf) {
		wl_buffer_destroy(c->buf);
	}
	if (c->pointer) {
		wl_pointer_destroy(c->pointer);
	}
}

static void destroy_binds(struct wayland_binds *b) {
	if (b->xwb) {
		xdg_wm_base_destroy(b->xwb);
	}
	if (b->shm) {
		wl_shm_destroy(b->shm);
	}
	if (b->seat) {
		wl_seat_destroy(b->seat);
	}
	if (b->comp) {
		wl_compositor_destroy(b->comp);
	}
}

static void wayland_terminate(void *ctx) {
	struct wayland *wl = ctx;
	egl_terminate(&wl->egl);
	if (wl->egl_window) {
		wl_egl_window_destroy(wl->egl_window);
	}

	if (wl->toplevel) {
		xdg_toplevel_destroy(wl->toplevel);
	}
	if (wl->xdg_surf) {
		xdg_surface_destroy(wl->xdg_surf);
	}
	if (wl->surf) {
		wl_surface_destroy(wl->surf);
	}

	destroy_keyboard(&wl->kb);
	destroy_cursor(&wl->cursor);
	destroy_binds(&wl->binds);

	if (wl->reg) {
		wl_registry_destroy(wl->reg);
	}
	if (wl->display) {
		wl_display_disconnect(wl->display);
	}
}

static void wayland_set_title(void *ctx, const char *title) {
	const struct wayland *wl = ctx;
	xdg_toplevel_set_title(wl->toplevel, title);
}

static void wayland_swap_buffers(void *ctx) {
	const struct wayland *wl = ctx;
	egl_swap(&wl->egl);
}

static bool has_events(struct wl_display *display, const int msecs) {
	while (wl_display_flush(display) == -1) {
		if (errno != EAGAIN) {
			return false;
		}
		struct pollfd can_write = {.fd = wl_display_get_fd(display),
			.events = POLLOUT};
		while (poll(&can_write, 1, -1) == -1) {
			if (errno != EAGAIN) {
				return false;
			}
		}
	}
	struct pollfd has_data = {.fd = wl_display_get_fd(display),
		.events = POLLIN};
	return poll(&has_data, 1, msecs) > 0 && has_data.revents & POLLIN;
}

static void wayland_poll(void *ctx, const long nsecs) {
	struct wayland *wl = ctx;
	const int msecs = (int)(nsecs / 1000000);
	while (wl_display_prepare_read(wl->display)) {
		wl_display_dispatch_pending(wl->display);
	}
	if (has_events(wl->display, msecs)) {
		wl_display_read_events(wl->display);
		wl_display_dispatch_pending(wl->display);
	} else {
		wl_display_cancel_read(wl->display);
	}
}

static void wayland_fullscreen(void *ctx, const int is_fullscreen) {
	struct wayland *wl = ctx;
	if (is_fullscreen) {
		xdg_toplevel_unset_fullscreen(wl->toplevel);
	} else {
		xdg_toplevel_set_fullscreen(wl->toplevel, NULL);
	}
}

static void wayland_resize(void *ctx, const int w, const int h) {
	struct wayland *wl = ctx;
	if (window_size_update(wl->pub, w, h) == trit_true) {
		wl_egl_window_resize(wl->egl_window, w, h, 0, 0);
	}
}

static bool test_mod(struct xkb_state *state, const char *name) {
	return (1 == xkb_state_mod_name_is_active(state, name,
		XKB_STATE_MODS_EFFECTIVE | XKB_STATE_LAYOUT_EFFECTIVE));
}

static uint8_t convert_by_codepoint(struct xkb_state *state, const uint32_t key) {
	const uint32_t codepoint = xkb_state_key_get_utf32(state, key + 8);
	switch (codepoint) {
	case ' ': case '=':
	case '<': case '>':
	case ',': case ';':
	case '.': case ':':
	case '-': case '+':
	case '*': case '/':
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		return (uint8_t)codepoint;
	}
	return 0;
}

static uint8_t convert_by_position(struct xkb_state *state, const uint32_t key) {
	switch (key) {
	case KEY_Q: case KEY_ESC: return 'Q';
	case KEY_F4:
		if (test_mod(state, XKB_MOD_NAME_ALT)) {
			return 'Q';
		}
		break;
	case KEY_W: case KEY_C:
		if (test_mod(state, XKB_MOD_NAME_CTRL)) {
			return 'Q';
		}
		break;

	case KEY_F: case KEY_F11: return 'F';
	case KEY_A: return 'A';
	case KEY_V: return 'V';
	case KEY_S: return 'S';
	case KEY_M: return 'M';

	case KEY_N: return 'N';
	case KEY_P: return 'P';

	case KEY_D: return 'D';
	case KEY_U: return 'U';

	case KEY_H: case KEY_LEFT: return 'H';
	case KEY_J: case KEY_DOWN: return 'J';
	case KEY_K: case KEY_UP: return 'K';
	case KEY_L: case KEY_RIGHT: return 'L';

	case KEY_Z: return 'Z';
	case KEY_X: return 'X';
	case KEY_I: return 'I';
	case KEY_O: return 'O';

	case KEY_END: return '=';
	case KEY_HOME:
		return test_mod(state, XKB_MOD_NAME_SHIFT) ? '1' : '0';
	case KEY_PAGEUP:
		return test_mod(state, XKB_MOD_NAME_SHIFT) ? '*' : '+';
	case KEY_PAGEDOWN:
		return test_mod(state, XKB_MOD_NAME_SHIFT) ? '/' : '-';

	case KEY_KPPLUS: return '+';
	case KEY_KPMINUS: return '-';
	case KEY_KPASTERISK: return '*';
	case KEY_KPSLASH: return '/';
	}
	return 0;
}

static int create_shm(void) {
	const int urand = open("/dev/urandom", O_RDONLY);
	int fd = -1;
	if (urand >= 0) {
		char name[10] = {'/'};
		unsigned char *buf = (unsigned char *)name + 1;
		const size_t len = sizeof(name) - 2;
		for (int tries = 0; tries < 4; ++tries) {
			if (read(urand, buf, len) == (ssize_t)len) {
				const unsigned char base_char = '/' + 1;
				for (size_t i = 0; i < len; ++i) {
					buf[i] = (unsigned char)(
						base_char + (buf[i] >> 2)
					);
				}
				fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL,
					0600);
				if (fd >= 0) {
					shm_unlink(name);
					break;
				}
			}
		}
		close(urand);
	}
	return fd;
}

static int alloc_shm(const int32_t dims) {
	const int fd = create_shm();
	if (fd >= 0) {
		for (int tries = 0; tries < 4; ++tries) {
			errno = 0;
			if (ftruncate(fd, dims) == 0) {
				return fd;
			} else if (errno != EINTR) {
				break;
			}
		}
		close(fd);
	}
	return fd;
}

static void draw_cursor(uint32_t *data, const int32_t w, const int32_t h) {
	const uint32_t five_shades_of_gray = 0x333333;
	for (int32_t y = 0; y < h; ++y) {
		const int32_t yp = y + 1;
		const int32_t hy = h - y;
		const int32_t limit = y < w ? yp : hy;
		for (int32_t x = 0; x < limit; ++x) {
			const bool border = (x > 0) & (x < y);
			const int tint = (x + 1 < hy) + (yp < w);
			uint32_t pix = 0xff000000;
			pix |= five_shades_of_gray * (uint32_t)(border && tint ? tint+2 : 1);
			data[y*w + x] = pix;
		}
	}
}

static struct wl_buffer * gen_cursor(struct wayland *wl, const int32_t height) {
	/* We generate an image instead of using wayland-cursor, as loading
	 * even a 24px pointer takes about 20ms. That's almost on par with the
	 * window creation time itself. */

	uint32_t *data;
	const int32_t width = (height * 182) >> 8;
	const int32_t pix_size = (int32_t)sizeof(*data);
	const int32_t stride = width * pix_size;
	const int32_t dims = height * stride;
	const int fd = alloc_shm(dims);
	if (fd < 0) {
		return NULL;
	}

	data = mmap(NULL, (size_t)dims, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(wl->binds.shm, fd, dims);
	struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, width,
		height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	const watch_t start = watch_look();
	draw_cursor(data, width, height);
	watch_report("Cursor generated", start, report_whocares);
	munmap(data, (size_t)dims);
	return buf;
}

static void set_cursor(struct wayland *wl) {
	int32_t size = 32; // The don't-care value used by everyone
	const char *cur_size = getenv("XCURSOR_SIZE");
	if (cur_size) {
		char *end;
		const unsigned long s = strtoul(cur_size, &end, 10);
		if (*end == 0 && s) {
			size = (int32_t)zumin(s, 256);
		}
	}

	struct wayland_cursor *c = &wl->cursor;
	c->buf = gen_cursor(wl, size);
	if (c->buf) {
		c->surf = wl_compositor_create_surface(wl->binds.comp);
		if (c->surf) {
			wl_surface_attach(c->surf, c->buf, 0, 0);
			wl_surface_commit(c->surf);
		}
	}
}

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
const int32_t w, const int32_t h, struct wl_array *states) {
	(void)toplevel;
	struct wayland *wl = data;
	wl->pub->win.focused = false;
	wl->pub->win.fullscreen = false;

	uint32_t *st;
	wl_array_for_each(st, states) {
		switch (*st) {
		case XDG_TOPLEVEL_STATE_ACTIVATED:
			wl->pub->win.focused = true;
			break;
		case XDG_TOPLEVEL_STATE_FULLSCREEN:
			wl->pub->win.fullscreen = true;
			break;
		}
	}
	wayland_resize(wl, w, h);
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
	(void)toplevel;
	struct wayland *wl = data;
	wl->pub->event.exit = true;
}

static void surface_configure(void *data, struct xdg_surface *surface,
const uint32_t serial) {
	(void)data;
	xdg_surface_ack_configure(surface, serial);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
const uint32_t serial, const uint32_t depressed, const uint32_t latched,
const uint32_t locked, const uint32_t group) {
	(void)keyboard; (void)serial;
	struct wayland *wl = data;
	if (wl->kb.state) {
		xkb_state_update_mask(wl->kb.state, depressed, latched,
			locked, 0, 0, group);
	}
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
const uint32_t serial, const uint32_t time, const uint32_t key,
const uint32_t state) {
	(void)keyboard; (void)serial; (void)time; (void)state;
	struct wayland *wl = data;
	if (wl->kb.state) {
		const enum key_action keyact =
			(state == WL_KEYBOARD_KEY_STATE_PRESSED)
				? key_press : key_release;

		const bool shift = test_mod(wl->kb.state, XKB_MOD_NAME_SHIFT);
		uint8_t c = convert_by_position(wl->kb.state, key);
		if (!c) {
			c = convert_by_codepoint(wl->kb.state, key);
		}
		window_key_add(&wl->pub->held_keys, keyact, c, shift);
	}
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
const uint32_t serial, struct wl_surface *surface) {
	(void)keyboard; (void)serial; (void)surface;
	struct wayland *wl = data;
	window_key_lift(&wl->pub->held_keys);
}

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
const uint32_t format, const int32_t fd, const uint32_t size) {
	(void)keyboard; (void)format; (void)size;
	struct wayland *wl = data;
	reset_xkb(&wl->kb);

	if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		char *str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (str != MAP_FAILED) {
			if (!wl->kb.ctx) {
				wl->kb.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
			}
			if (wl->kb.ctx) {
				wl->kb.keymap = xkb_keymap_new_from_string(
					wl->kb.ctx, str,
					XKB_KEYMAP_FORMAT_TEXT_V1,
					XKB_KEYMAP_COMPILE_NO_FLAGS);
				if (wl->kb.keymap) {
					wl->kb.state = xkb_state_new(wl->kb.keymap);
				}
			}
			munmap(str, size);
		}
	}
	close(fd);
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
const uint32_t time, const uint32_t axis, const wl_fixed_t value) {
	(void)pointer; (void)time;
	struct wayland *wl = data;
	struct window_cursor *cur = &wl->pub->win.cur;
	struct window_cursor_axis *a = (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		? &cur->y : &cur->x;
	window_scroll_axis(a, wl_fixed_to_double(value));
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
const uint32_t time, const wl_fixed_t x, const wl_fixed_t y) {
	(void)pointer; (void)time;
	struct wayland *wl = data;
	window_cursor_move(wl->pub, wl_fixed_to_double(x),
		wl_fixed_to_double(y));
}

static void pointer_button(void *data, struct wl_pointer *pointer,
const uint32_t serial, const uint32_t time, const uint32_t button,
const uint32_t state) {
	(void)pointer; (void)serial; (void)time;
	struct wayland *wl = data;
	if (button == BTN_LEFT) {
		wl->pub->win.pressed =
			(state == WL_POINTER_BUTTON_STATE_PRESSED);
	}
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
const uint32_t serial, struct wl_surface *surface, const wl_fixed_t x,
const wl_fixed_t y) {
	(void)surface; (void)x; (void)y;
	struct wayland *wl = data;
	struct window_cursor *c = &wl->pub->win.cur;
	c->x.pos = (float)wl_fixed_to_double(x);
	c->y.pos = (float)wl_fixed_to_double(y);

	struct wayland_cursor *wc = &wl->cursor;
	if (wc->buf) {
		wl_pointer_set_cursor(pointer, serial, wc->surf, 0, 0);
	}
}

static const struct wl_pointer_listener listen_pointer = {
	.enter = pointer_enter,
	.leave = null_function,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = null_function,
	.axis_source = null_function,
	.axis_stop = null_function,
	.axis_discrete = null_function,
};
static const struct wl_keyboard_listener listen_keyboard = {
	.keymap = keyboard_keymap,
	.enter = null_function,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = null_function,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
const uint32_t caps) {
	struct wayland *wl = data;
	struct window_common *win = &wl->pub->win;
	struct wayland_keyboard *k = &wl->kb;
	if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
		if (!k->keyboard) {
			k->keyboard = wl_seat_get_keyboard(seat);
			wl_keyboard_add_listener(k->keyboard, &listen_keyboard, wl);
		}
	} else {
		if (k->keyboard) {
			wl_keyboard_release(k->keyboard);
			k->keyboard = NULL;
			reset_xkb(k);
		}
	}
	struct wayland_cursor *c = &wl->cursor;
	if (caps & WL_SEAT_CAPABILITY_POINTER) {
		if (!c->pointer) {
			win->pressed = false;
			c->pointer = wl_seat_get_pointer(seat);
			wl_pointer_add_listener(c->pointer, &listen_pointer, wl);
		}
	} else {
		if (c->pointer) {
			wl_pointer_release(c->pointer);
			c->pointer = NULL;
		}
	}
}

static void wm_base_ping(void *data, struct xdg_wm_base *xwb,
const uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(xwb, serial);
}

static const struct wl_seat_listener listen_seat = {
	.capabilities = seat_capabilities,
	.name = null_function,
};
static const struct xdg_wm_base_listener listen_wm_base = {
	.ping = wm_base_ping,
};

static void reg_global(void *data, struct wl_registry *reg, const uint32_t name,
const char *interface, const uint32_t version) {
	(void)version;
	struct wayland *wl = data;
	struct wayland_binds *b = &wl->binds;
	if (!strcmp(interface, wl_compositor_interface.name)) {
		b->comp = wl_registry_bind(reg, name, &wl_compositor_interface, 1);
	} else if (!strcmp(interface, wl_shm_interface.name)) {
		b->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (!strcmp(interface, wl_seat_interface.name)) {
		b->seat = wl_registry_bind(reg, name, &wl_seat_interface, 1);
		wl_seat_add_listener(b->seat, &listen_seat, wl);
	} else if (!strcmp(interface, xdg_wm_base_interface.name)) {
		b->xwb = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(b->xwb, &listen_wm_base, NULL);
	}
}

static const struct wl_registry_listener listen_reg = {
	.global = reg_global,
	.global_remove = null_function,
};
static const struct xdg_surface_listener listen_surface = {
	.configure = surface_configure,
};
static const struct xdg_toplevel_listener listen_toplevel = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

const char * wayland_init(struct wayland *wl, struct window_public *pub) {
	*wl = (struct wayland){0};

	struct wu_conf *conf = &pub->image.conf;
	wl->pub = pub;

	wl->display = wl_display_connect(NULL);
	if (!wl->display) {
		return "Couldn't connect to display";
	}

	wl->reg = wl_display_get_registry(wl->display);
	if (!wl->reg) {
		return "Couldn't obtain registry";
	}

	wl_registry_add_listener(wl->reg, &listen_reg, wl);
	wl_display_roundtrip(wl->display);
	if (!wl->binds.comp) {
		return "Failed to bind to compositor";
	} else if (!wl->binds.seat) {
		return "Failed to bind to seat";
	} else if (!wl->binds.xwb) {
		return "Failed to bind to wm_base";
	}
	if (wl->binds.shm) {
		set_cursor(wl);
	}

	wl->surf = wl_compositor_create_surface(wl->binds.comp);
	if (!wl->surf) {
		return "Failed to create surface";
	}

	wl->egl_window = wl_egl_window_create(wl->surf,
		(int)conf->initial_size.w, (int)conf->initial_size.h);
	if (!wl->egl_window) {
		return "Failed to get EGL window";
	}

	wl->xdg_surf = xdg_wm_base_get_xdg_surface(wl->binds.xwb, wl->surf);
	if (!wl->xdg_surf) {
		return "Failed to get xdg_surface";
	}
	xdg_surface_add_listener(wl->xdg_surf, &listen_surface, NULL);

	wl->toplevel = xdg_surface_get_toplevel(wl->xdg_surf);
	if (!wl->toplevel) {
		return "Failed to get toplevel";
	}
	xdg_toplevel_set_app_id(wl->toplevel, WU_CANON_NAME);
	xdg_toplevel_add_listener(wl->toplevel, &listen_toplevel, wl);

	wl_surface_commit(wl->surf);
	wl_display_roundtrip(wl->display);

	const char *err = egl_init(&wl->egl,
		(EGLNativeDisplayType)wl->display, wl->egl_window, 0,
		conf->bg[3] < 0xff);
	if (err) {
		egl_print_error();
		return err;
	}

	pub->win.fn = (struct window_fn) {
		.title = wayland_set_title,
		.resize = wayland_resize,
		.fullscreen = wayland_fullscreen,
		.poll = wayland_poll,
		.swap_buffers = wayland_swap_buffers,
		.terminate = wayland_terminate,
	};
	return NULL;
}

static void wayland_offscreen_terminate(void *ctx) {
	struct wayland_offscreen *wl = ctx;
	if (wl->egl_display) {
		egl_offscreen_terminate(wl->egl_display);
	}
	if (wl->display) {
		wl_display_disconnect(wl->display);
	}
}

const char * wayland_offscreen_init(struct wayland_offscreen *wl,
window_fn_ctx_t *terminate) {
	wl->display = wl_display_connect(NULL);
	if (!wl->display) {
		return "Couldn't connect to display";
	}
	const char *err = egl_offscreen_init(&wl->egl_display, NULL, wl->display);
	if (err) {
		wayland_offscreen_terminate(wl);
	} else {
		*terminate = wayland_offscreen_terminate;
	}
	return err;
}
