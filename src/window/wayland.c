#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <linux/input-event-codes.h>

#include "wayland.h"
#include "../common.h"
#include "../raster/text.h"

static bool test_mod(struct xkb_state *state, const char *name) {
	return (1 == xkb_state_mod_name_is_active(state, name,
		XKB_STATE_MODS_EFFECTIVE | XKB_STATE_LAYOUT_EFFECTIVE));
}

static uint8_t convert_by_codepoint(struct xkb_state *state, const uint32_t key) {
	const uint32_t codepoint = xkb_state_key_get_utf32(state, key + 8);
	switch (codepoint) {
	case ' ':
	case ',': case ';':
	case '.': case ':':
	case '-': case '+':
	case '<': case '>':
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
	case KEY_M: return 'M';

	case KEY_N: return 'N';
	case KEY_P: return 'P';

	case KEY_R: case KEY_F5: return 'R';

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

	case KEY_END: return '0';
	case KEY_HOME: return '1';
	case KEY_PAGEUP: case KEY_KPPLUS: return '+';
	case KEY_PAGEDOWN: case KEY_KPMINUS: return '-';
	}
	return 0;
}

static int create_shm(void) {
	const int urand = open("/dev/urandom", O_RDONLY);
	int fd = -1;
	if (urand >= 0) {
		unsigned char name[10] = {'/'};
		const size_t len = sizeof(name) - 2;
		for (int tries = 0; tries < 4; ++tries) {
			if ((size_t)read(urand, name + 1, len) == len) {
				for (size_t i = 0; i < len; ++i) {
					name[i] = (unsigned char)
						(('/' + 1) + (name[i] >> 2));
				}
				fd = shm_open((char *)name,
					O_RDWR | O_CREAT | O_EXCL, 0600);
				if (fd >= 0) {
					shm_unlink((char *)name);
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

static struct wl_buffer * gen_cursor(struct wayland *wl, const int32_t height) {
	/* We've decided to generate an image instead of using wayland-cursor,
	 * as loading even a 24px pointer takes about 20ms.
	 * That's about 80% of the time it takes to set up the window alone. */

	// Where these come from is left as an exercise for the maintainer.
	const uint32_t five_shades_of_gray = 0x333333;
	const uint32_t uheight = (uint32_t)height;
	const uint32_t uwidth = uheight * 2372656 / five_shades_of_gray;

	const int32_t width = (int32_t)uwidth;

	uint32_t *data;
	const int32_t pix_size = (int32_t)sizeof(*data);
	const int32_t stride = width * pix_size;
	const int32_t dims = height * stride;
	const int fd = alloc_shm(dims);
	if (fd < 0) {
		return false;
	}

	data = mmap(NULL, (size_t)dims, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return false;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(wl->binds.shm, fd, dims);
	struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, width,
		height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	for (uint32_t y = 0; y < uheight; ++y) {
		for (uint32_t x = 0; x < uwidth; ++x) {
			const uint32_t pos = y*uwidth + x;
			const bool alpha = x <= y
				&& (y + x <= uheight - 1 || y <= uwidth - 1);
			if (alpha) {
				const uint32_t triangle = (x > 0) && (x < y);
				uint32_t gray = triangle
					&& ((y + x < uheight - 1) || (y < uwidth - 1));
				if (gray) {
					gray = five_shades_of_gray * (triangle * 2u
						+ (y + x < uheight - 1)
						+ (y < uwidth - 1)
					);
				}
				data[pos] = 0xff000000 + gray;
			}
		}
	}
	munmap(data, (size_t)dims);
	return buf;
}

static void prepare_cursor(struct wayland *wl) {
	int32_t size = 32; // The don't-care value used by everyone
	const char *env_size = getenv("XCURSOR_SIZE");
	if (env_size) {
		const size_t max_digits = 4;
		struct text_parser tp = text_parser_mem(max_digits, env_size);
		text_fast_t tmp;
		if (env_size[text_get_uint_unsafe(&tp, max_digits, &tmp)] == 0 && tmp) {
			size = imin(256, (int32_t)tmp);
		}
	}

	clock_t start = clock();
	struct wayland_cursor *c = &wl->cursor;
	c->buf = gen_cursor(wl, size);
	if (c->buf) {
		printf("cursor created in %f\n", clock_ellapsed(start));
		c->surf = wl_compositor_create_surface(wl->binds.comp);
		if (c->surf) {
			wl_surface_attach(c->surf, c->buf, 0, 0);
			wl_surface_commit(c->surf);
		}
	}
}

static void reset_xkb(struct xkb *xkb) {
	if (xkb->state) {
		xkb_state_unref(xkb->state);
		xkb->state = NULL;
	}
	if (xkb->keymap) {
		xkb_keymap_unref(xkb->keymap);
		xkb->keymap = NULL;
	}
}

void wayland_terminate(struct wayland *wl) {
	reset_xkb(&wl->xkb);
	if (wl->xkb.ctx) {
		xkb_context_unref(wl->xkb.ctx);
	}
	if (wl->egl_window) {
		wl_egl_window_destroy(wl->egl_window);
	}
	if (wl->display) {
		wl_display_disconnect(wl->display);
	}
}

void wayland_set_title(const struct wayland *wl, const char *title) {
	xdg_toplevel_set_title(wl->toplevel, title);
}

bool wayland_swap_buffers(const struct wayland *wl) {
	return egl_swap(&wl->egl);
}

void wayland_poll(struct wayland *wl) {
	wl_display_dispatch_pending(wl->display);
}

void wayland_fullscreen(struct wayland *wl) {
	if (wl->fullscreen) {
		xdg_toplevel_unset_fullscreen(wl->toplevel);
	} else {
		xdg_toplevel_set_fullscreen(wl->toplevel, NULL);
	}
	wl->fullscreen = !wl->fullscreen;
}

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
const int32_t width, const int32_t height, struct wl_array *states) {
	(void)toplevel;
	struct wayland *wl = data;
	wl->active = false;

	uint32_t *st;
	wl_array_for_each(st, states) {
		switch (*st) {
		case XDG_TOPLEVEL_STATE_ACTIVATED:
			wl->active = true;
			break;
		}
	}

	if (window_size_update(wl->pub, (unsigned)width, (unsigned)height)
	== trit_true) {
		wl_egl_window_resize(wl->egl_window, width, height, 0, 0);
	}
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
	(void)toplevel;
	struct wayland *wl = data;
	wl->pub->event.window = close_window;
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
	if (wl->xkb.state) {
		xkb_state_update_mask(wl->xkb.state, depressed, latched,
			locked, 0, 0, group);
	}
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
const uint32_t serial, const uint32_t time, const uint32_t key,
const uint32_t state) {
	(void)keyboard; (void)serial; (void)time; (void)state;
	struct wayland *wl = data;
	if (wl->xkb.state) {
		const enum key_action keyact =
			(state == WL_KEYBOARD_KEY_STATE_PRESSED)
				? key_press : key_release;

		const bool shift = test_mod(wl->xkb.state, XKB_MOD_NAME_SHIFT);
		uint8_t c = convert_by_position(wl->xkb.state, key);
		if (!c) {
			c = convert_by_codepoint(wl->xkb.state, key);
		}
		if (c) {
			struct wu_keymap *held_keys = &wl->pub->held_keys;
			event_add(held_keys, keyact, c, shift);
		}
	}
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
const uint32_t serial, struct wl_surface *surface) {
	(void)keyboard; (void)serial; (void)surface;
	struct wayland *wl = data;
	event_lift(&wl->pub->held_keys);
}

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
const uint32_t format, const int32_t fd, const uint32_t size) {
	(void)keyboard; (void)format; (void)size;
	struct wayland *wl = data;
	reset_xkb(&wl->xkb);

	if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		char *str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (str != MAP_FAILED) {
			if (!wl->xkb.ctx) {
				wl->xkb.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
			}
			if (wl->xkb.ctx) {
				wl->xkb.keymap = xkb_keymap_new_from_string(
					wl->xkb.ctx, str,
					XKB_KEYMAP_FORMAT_TEXT_V1,
					XKB_KEYMAP_COMPILE_NO_FLAGS);
				if (wl->xkb.keymap) {
					wl->xkb.state = xkb_state_new(wl->xkb.keymap);
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
	struct window_cursor_axis *a = (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		? &wl->cursor.state.y : &wl->cursor.state.x;
	window_scroll_axis(a, wl_fixed_to_double(value));
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
const uint32_t time, const wl_fixed_t x, const wl_fixed_t y) {
	(void)pointer; (void)time;
	struct wayland *wl = data;
	window_cursor_apply_diff(&wl->cursor.state, wl->pub,
		wl_fixed_to_double(x), wl_fixed_to_double(y));
}

static void pointer_button(void *data, struct wl_pointer *pointer,
const uint32_t serial, const uint32_t time, const uint32_t button,
const uint32_t state) {
	(void)pointer; (void)serial; (void)time;
	struct wayland *wl = data;
	if (button == BTN_LEFT) {
		wl->cursor.state.pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
	}
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
const uint32_t serial, struct wl_surface *surface, const wl_fixed_t x,
const wl_fixed_t y) {
	(void)surface; (void)x; (void)y;
	struct wayland *wl = data;
	struct wayland_cursor *c = &wl->cursor;
	c->state.x.pos = (float)wl_fixed_to_double(x);
	c->state.y.pos = (float)wl_fixed_to_double(y);
	if (c->buf) {
		wl_pointer_set_cursor(pointer, serial, c->surf, 0, 0);
	}
}

static void seat_capabilities(void *data, struct wl_seat *seat,
const uint32_t caps) {
	struct wayland *wl = data;
	if (caps & WL_SEAT_CAPABILITY_POINTER) {
		if (!wl->pointer) {
			wl->cursor.state.pressed = false;
			wl->pointer = wl_seat_get_pointer(seat);
			wl_pointer_add_listener(wl->pointer, &wl->listen.pointer, wl);
		}
	} else {
		if (wl->pointer) {
			wl_pointer_release(wl->pointer);
			wl->pointer = NULL;
		}
	}
	if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
		if (!wl->keyboard) {
			wl->keyboard = wl_seat_get_keyboard(seat);
			wl_keyboard_add_listener(wl->keyboard, &wl->listen.keyboard, wl);
		}
	} else {
		if (wl->keyboard) {
			wl_keyboard_release(wl->keyboard);
			wl->keyboard = NULL;
			reset_xkb(&wl->xkb);
		}
	}
}

static void wm_base_ping(void *data, struct xdg_wm_base *xwb,
const uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(xwb, serial);
}

static void reg_global(void *data, struct wl_registry *reg, uint32_t name,
const char *interface, uint32_t version) {
	(void)version;
	struct wayland *wl = data;
	struct wayland_binds *b = &wl->binds;
	if (!strcmp(interface, wl_compositor_interface.name)) {
		b->comp = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
	} else if (!strcmp(interface, wl_seat_interface.name)) {
		b->seat = wl_registry_bind(reg, name, &wl_seat_interface, 7);
		wl_seat_add_listener(b->seat, &wl->listen.seat, wl);
	} else if (!strcmp(interface, wl_shm_interface.name)) {
		b->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (!strcmp(interface, xdg_wm_base_interface.name)) {
		b->xwb = wl_registry_bind(reg, name, &xdg_wm_base_interface, 2);
		xdg_wm_base_add_listener(b->xwb, &wl->listen.wm_base, NULL);
	}

}

static void reg_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	(void)data; (void)reg; (void)name;
}

const char * wayland_init(struct wayland *wl, struct window_public *pub) {
	*wl = (struct wayland){0};

	struct wu_conf *conf = &pub->image.conf;
	wl->pub = pub;

	wl->display = wl_display_connect(NULL);
	if (!wl->display) {
		return "Wayland: Couldn't connect to display";
	}

	wl->reg = wl_display_get_registry(wl->display);
	if (!wl->reg) {
		return "Wayland: Couldn't obtain registry";
	}

	wl->listen = (struct wayland_listeners) {
		.reg = {
			.global = reg_global,
			.global_remove = reg_global_remove,
		},
		.seat = {
			.capabilities = seat_capabilities,
			.name = null_function,
		},
		.pointer = {
			.enter = pointer_enter,
			.leave = null_function,
			.motion = pointer_motion,
			.button = pointer_button,
			.axis = pointer_axis,
			.frame = null_function,
			.axis_source = null_function,
			.axis_stop = null_function,
			.axis_discrete = null_function,
		},
		.keyboard = {
			.keymap = keyboard_keymap,
			.enter = null_function,
			.leave = keyboard_leave,
			.key = keyboard_key,
			.modifiers = keyboard_modifiers,
			.repeat_info = null_function,
		},
		.wm_base = {.ping = wm_base_ping},
		.surface = {.configure = surface_configure},
		.toplevel = {
			.configure = toplevel_configure,
			.close = toplevel_close,
		},
	};

	wl_registry_add_listener(wl->reg, &wl->listen.reg, wl);
	wl_display_roundtrip(wl->display);

	if (!wl->binds.comp) {
		return "Wayland: Failed to bind to compositor";
	} else if (!wl->binds.seat) {
		return "Wayland: Failed to bind to seat";
	} else if (!wl->binds.xwb) {
		return "Wayland: Failed to bind to wm_base";
	}
	if (wl->binds.shm) {
		prepare_cursor(wl);
	}

	wl->surf = wl_compositor_create_surface(wl->binds.comp);
	if (!wl->surf) {
		return "Wayland: Failed to create surface";
	}

	wl->xdg_surf = xdg_wm_base_get_xdg_surface(wl->binds.xwb, wl->surf);
	if (!wl->xdg_surf) {
		return "Wayland: Failed to get xdg_surface";
	}
	xdg_surface_add_listener(wl->xdg_surf, &wl->listen.surface, NULL);

	wl->toplevel = xdg_surface_get_toplevel(wl->xdg_surf);
	if (!wl->toplevel) {
		return "Wayland: Failed to get toplevel";
	}
	xdg_toplevel_add_listener(wl->toplevel, &wl->listen.toplevel, wl);

	wl->egl_window = wl_egl_window_create(wl->surf,
		(int)conf->initial_size.w, (int)conf->initial_size.h);
	if (!wl->egl_window) {
		return "Wayland: Failed to get EGL window";
	}
	const bool alpha = conf->bg[3] < 0xff;
	const char *err = egl_init(&wl->egl, (EGLNativeDisplayType)wl->display,
		wl->egl_window, 0, alpha);
	if (err) {
		egl_print_error();
		return err;
	}
	xdg_toplevel_set_app_id(wl->toplevel, WU_CANON_NAME);

	wl_surface_commit(wl->surf);
	wl_display_roundtrip(wl->display);

	// Get window size
	wayland_swap_buffers(wl);
//	wl_display_dispatch(wl->display);
	return NULL;
}

void wayland_offscreen_terminate(struct wayland_offscreen *wl) {
	if (wl->egl_display) {
		egl_offscreen_terminate(wl->egl_display);
	}
	if (wl->display) {
		wl_display_disconnect(wl->display);
	}
}

const char * wayland_offscreen_init(struct wayland_offscreen *wl) {
	wl->display = wl_display_connect(NULL);
	if (!wl->display) {
		return "Wayland: Couldn't connect to display";
	}
	const char *err = egl_offscreen_init(&wl->egl_display, wl->display);
	if (err) {
		wayland_offscreen_terminate(wl);
	}
	return err;
}
