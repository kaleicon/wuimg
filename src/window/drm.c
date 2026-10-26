// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <unistd.h>

#include <epoxy/egl.h>
#include <gbm.h>
#include <xf86drmMode.h>
#include <xf86drm.h>

#include <libinput.h>
#include <libseat.h>
#include <linux/input-event-codes.h>

#include "misc/math.h"
#include "misc/term.h"
#include "window/base.h"
#include "window/egl.h"
#include "opengl.h"

struct seat_device {
	int id;
	int fd;
};

enum input_modifier {
	input_left_shift = 1 << 0,
	input_right_shift = 1 << 1,
	input_shift = input_left_shift | input_right_shift,

	input_left_ctrl = 1 << 2,
	input_right_ctrl = 1 << 3,
	input_ctrl = input_left_ctrl | input_right_ctrl,

	input_left_alt = 1 << 4,
	input_right_alt = 1 << 5,
	input_alt = input_left_alt | input_right_alt,
};

struct drm_context {
	struct drm_drm {
		uint32_t connector_id;
		uint32_t crtc_id;
		uint32_t fb_id[2];
		int fd;
	} drm;

	struct drm_gbm {
		struct gbm_device *device;
		struct gbm_surface *surface;
		struct gbm_bo *bo;
	} gbm;

	struct egl egl;

	struct libseat *seat;
	struct libinput *li;

	enum input_modifier mod;

	unsigned nr_devices;
	struct seat_device device[32];
};

struct drm_offscreen {
	int fd;
	struct gbm_device *device;
	EGLDisplay egl_display;
};

static const uint32_t WU_GBM_FORMAT = GBM_FORMAT_XRGB8888;


static void drm_terminate(struct window_context *wctx) {
	struct drm_context *ctx = wctx->win.ctx;

	libinput_unref(ctx->li);
	libseat_close_seat(ctx->seat);

	eglTerminate(ctx->egl.display);

	if (ctx->drm.fd >= 0) {
		drmModeRmFB(ctx->drm.fd, ctx->drm.fb_id[0]);
		drmModeRmFB(ctx->drm.fd, ctx->drm.fb_id[1]);
	}
	if (ctx->gbm.bo) {
		gbm_bo_destroy(ctx->gbm.bo);
	}
	if (ctx->gbm.surface) {
		gbm_surface_destroy(ctx->gbm.surface);
	}
	if (ctx->gbm.device) {
		gbm_device_destroy(ctx->gbm.device);
	}
	if (ctx->drm.fd >= 0) {
		close(ctx->drm.fd);
	}
	free(ctx);
}

static void fb_destroy_fn(struct gbm_bo *bo, void *data) {
	uint32_t *fb_ptr = data;
	drmModeRmFB(gbm_bo_get_fd(bo), *fb_ptr);
	*fb_ptr = 0;
}

static uint32_t get_framebuffer(struct drm_drm *drm,
struct gbm_surface *surface, struct window_context *wctx, struct gbm_bo **bo) {
	*bo = gbm_surface_lock_front_buffer(surface);
	uint32_t *fb_ptr = gbm_bo_get_user_data(*bo);
	if (fb_ptr) {
		if (*fb_ptr) {
			return *fb_ptr;
		}
	} else {
		for (size_t i = 0; i < ARRAY_LEN(drm->fb_id); ++i) {
			if (drm->fb_id[i] == 0) {
				fb_ptr = drm->fb_id + i;
				break;
			}
		}
		if (!fb_ptr) {
			term_line_put("no free framebuffers :o", stderr);
			return 0;
		}
	}

	const uint32_t width = gbm_bo_get_width(*bo);
	const uint32_t height = gbm_bo_get_height(*bo);
	const uint32_t format = gbm_bo_get_format(*bo);

	uint32_t handles[4] = {gbm_bo_get_handle(*bo).u32, 0};
	uint32_t pitches[4] = {gbm_bo_get_stride(*bo), 0};
	uint32_t offsets[4] = {0};

	const int fail = drmModeAddFB2(drm->fd, width, height, format, handles,
		pitches, offsets, fb_ptr, 0);
	if (fail) {
		*fb_ptr = 0;
	} else {
		gbm_bo_set_user_data(*bo, fb_ptr, fb_destroy_fn);
		window_size_update(wctx, (int)width, (int)height);
	}
	return *fb_ptr;
}

static void flipper_fn(int _fd, unsigned int _sequence, unsigned int _sec,
unsigned int _usec, void *data) {
	(void)_fd;
	(void)_sequence;
	(void)_sec;
	(void)_usec;

	bool *flipped = data;
	*flipped = true;
}

static void drm_swap_buffers(struct window_context *wctx) {
	struct drm_context *ctx = wctx->win.ctx;
	egl_swap(&ctx->egl);

	struct drm_drm *drm = &ctx->drm;
	struct gbm_bo *next_bo;
	const uint32_t fb_id = get_framebuffer(drm, ctx->gbm.surface, wctx,
		&next_bo);
	if (!fb_id) {
		return;
	}

	bool flipped = false;
	int status = drmModePageFlip(drm->fd, drm->crtc_id, fb_id,
		DRM_MODE_PAGE_FLIP_EVENT, &flipped);
	if (status) {
		term_line_put("page flip failed\n", stderr);
		return;
	}

	const struct timespec timeout = {1, 0};
	drmEventContext evctx = {
		.version = 2,
		.page_flip_handler = flipper_fn,
	};
	while (!flipped) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(drm->fd, &fds);

		errno = 0;
		status = pselect(drm->fd + 1, &fds, NULL, NULL, &timeout, NULL);
		if (status < 0) {
			perror("Failed to poll DRM descriptor");
			break;
		} else if (status == 0) {
			term_line_put("DRM descriptor poll timed out after 1 second\n",
				stderr);
			break;
		}

		drmHandleEvent(drm->fd, &evctx);
	}

	gbm_surface_release_buffer(ctx->gbm.surface, ctx->gbm.bo);
	ctx->gbm.bo = next_bo;
}

static uint8_t set_mod(struct drm_context *ctx, const enum key_action act,
const enum input_modifier mod) {
	if (act == key_pressed) {
		ctx->mod |= mod;
	} else {
		ctx->mod &= ~mod;
	}
	return 0;
}

static uint8_t vt(struct drm_context *ctx, const enum key_action act,
const int vt) {
	if (act == key_pressed && (ctx->mod & input_alt)) {
		libseat_switch_session(ctx->seat, vt);
	}
	return 0;
}

static uint8_t consume_key(struct drm_context *ctx, const uint32_t key,
const enum key_action act) {
        switch (key) {
	case KEY_LEFTSHIFT: return set_mod(ctx, act, input_left_shift);
	case KEY_RIGHTSHIFT: return set_mod(ctx, act, input_right_shift);
	case KEY_LEFTCTRL: return set_mod(ctx, act, input_left_ctrl);
	case KEY_RIGHTCTRL: return set_mod(ctx, act, input_right_ctrl);
	case KEY_LEFTALT: case KEY_LEFTMETA: return set_mod(ctx, act, input_left_alt);
	case KEY_RIGHTALT: case KEY_RIGHTMETA: return set_mod(ctx, act, input_right_alt);

	case KEY_F1: return vt(ctx, act, 1);
	case KEY_F2: return vt(ctx, act, 2);
	case KEY_F3: return vt(ctx, act, 3);
	case KEY_F4: return vt(ctx, act, 4);
	case KEY_F5: return vt(ctx, act, 5);
	case KEY_F6: return vt(ctx, act, 6);
	case KEY_F7: return vt(ctx, act, 7);
	case KEY_F8: return vt(ctx, act, 8);
	case KEY_F9: return vt(ctx, act, 9);
	case KEY_F10: return vt(ctx, act, 10);
	case KEY_F11: return vt(ctx, act, 11);
	case KEY_F12: return vt(ctx, act, 12);

	case KEY_W: case KEY_C:
		if ((ctx->mod & input_ctrl)) {
        case KEY_Q: case KEY_ESC:
			return 'Q';
		}
		break;
        case KEY_A: return 'A';
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

	case KEY_1: case KEY_KP1: return '1';
	case KEY_2: case KEY_KP2: return '2';
	case KEY_3: case KEY_KP3: return '3';
	case KEY_4: case KEY_KP4: return '4';
	case KEY_5: case KEY_KP5: return '5';
	case KEY_6: case KEY_KP6: return '6';
	case KEY_7: case KEY_KP7: return '7';
	case KEY_8: case KEY_KP8: return '8';
	case KEY_9: case KEY_KP9: return '9';

        case KEY_END: return '=';
        case KEY_HOME: case KEY_0: case KEY_KP0: return '0';
        case KEY_PAGEUP: return '+';
        case KEY_PAGEDOWN: return '-';

        case KEY_KPPLUS: return '+';
        case KEY_KPMINUS: return '-';
        case KEY_KPASTERISK: return '*';
        case KEY_KPSLASH: return '/';
        }
        return 0;
}

static void handle_keyboard_ev(struct window_context *wctx,
struct drm_context *ctx, struct libinput_event *ev) {
	struct libinput_event_keyboard *kb = libinput_event_get_keyboard_event(ev);
	if (kb) {
		const enum libinput_key_state state = libinput_event_keyboard_get_key_state(kb);
		const enum key_action act = state == LIBINPUT_KEY_STATE_PRESSED
			? key_pressed : key_released;
		const uint8_t c = consume_key(ctx,
			libinput_event_keyboard_get_key(kb), act);

		window_key_add(wctx, act, c, ctx->mod & input_shift);
	}
}

static void input_poll(struct window_context *wctx, const int msecs) {
	struct drm_context *ctx = wctx->win.ctx;

	struct pollfd has_data[] = {
		{.fd = libseat_get_fd(ctx->seat), .events = POLLIN},
		{.fd = libinput_get_fd(ctx->li), .events = POLLIN},
		{.fd = STDIN_FILENO, .events = POLLIN},
	};
	if (poll(has_data, ARRAY_LEN(has_data), msecs) > 0
	&& ((has_data[0].revents | has_data[1].revents) & POLLIN)) {
		libseat_dispatch(ctx->seat, 0);
		libinput_dispatch(ctx->li);

		struct libinput_event *ev;
		while ((ev = libinput_get_event(ctx->li)) != NULL) {
			switch (libinput_event_get_type(ev)) {
			case LIBINPUT_EVENT_KEYBOARD_KEY:
				handle_keyboard_ev(wctx, ctx, ev);
				break;
			default: break;
			}
			libinput_event_destroy(ev);
			libinput_dispatch(ctx->li);
		}
	}
}

static void input_log(struct libinput *li,
const enum libinput_log_priority priority, const char *format, va_list args) {
	(void)li; (void)priority;
	fputs("libinput: ", stderr);
	vfprintf(stderr, format, args);
}

static void seat_log(const enum libseat_log_level level, const char *format,
va_list args) {
	(void)level;
	fputs("libseat: ", stderr);
	vfprintf(stderr, format, args);
	fputc('\n', stderr);
}

static int li_open(const char *path, const int flags, void *data) {
	(void)flags;
	struct drm_context *ctx = data;
	int res = -ENOMEM;
	if (ctx->nr_devices < ARRAY_LEN(ctx->device)) {
		struct seat_device *dev = ctx->device + ctx->nr_devices;
		dev->id = libseat_open_device(ctx->seat, path, &dev->fd);
		if (dev->id == -1) {
			res = -errno;
		} else {
			res = dev->fd;
			++ctx->nr_devices;
		}
	}
	return res;
}

static void li_close(const int fd, void *data) {
	struct drm_context *ctx = data;
	for (size_t i = 0; i < ctx->nr_devices; ++i) {
		if (ctx->device[i].fd == fd) {
			libseat_close_device(ctx->seat, ctx->device[i].id);
			--ctx->nr_devices;
			memmove(ctx->device + i, ctx->device + i + 1,
				(ctx->nr_devices - i)*sizeof(*ctx->device));
			break;
		}
	}
}

static const struct libinput_interface li_interface = {
	.open_restricted = li_open,
	.close_restricted = li_close,
};

static void seat_enable(struct libseat *seat, void *data) {
	(void)seat;
	struct window_context *wctx = data;
	wctx->win.focused = true;
	wctx->gl.update = true;
	struct drm_context *ctx = wctx->win.ctx;
	if (ctx->li) {
		libinput_resume(ctx->li);
	}
}

static void seat_disable(struct libseat *seat, void *data) {
	struct window_context *wctx = data;
	wctx->win.focused = false;
	window_key_lift(wctx);
	libseat_disable_seat(seat);
	struct drm_context *ctx = wctx->win.ctx;
	if (ctx->li) {
		libinput_suspend(ctx->li);
	}
}

static const struct libseat_seat_listener seat_listener = {
	.enable_seat = seat_enable,
	.disable_seat = seat_disable,
};

static const char * input_setup(struct window_context *wctx,
struct drm_context *ctx) {
	const bool debug = getenv("WU_DEBUG");
	libseat_set_log_handler(seat_log);
	libseat_set_log_level(debug
		? LIBSEAT_LOG_LEVEL_DEBUG : LIBSEAT_LOG_LEVEL_ERROR);
	ctx->seat = libseat_open_seat(&seat_listener, wctx);
	if (!ctx->seat) {
		return "Failed to open seat";
	}
	libseat_dispatch(ctx->seat, 0);

	struct udev *udev = udev_new();
	if (!udev) {
		return "Failed to create udev context";
	}
	ctx->li = libinput_udev_create_context(&li_interface, ctx, udev);
	udev_unref(udev);
	if (!ctx->li) {
		return "Failed to create libinput context";
	}
	libinput_log_set_handler(ctx->li, input_log);
	libinput_log_set_priority(ctx->li, debug
		? LIBINPUT_LOG_PRIORITY_DEBUG : LIBINPUT_LOG_PRIORITY_ERROR);
	return libinput_udev_assign_seat(ctx->li, libseat_seat_name(ctx->seat)) == 0
		? NULL : "Failed to assign seat to libinput";
}

static bool mode_set(struct window_context *wctx, struct drm_context *ctx,
drmModeModeInfo *mode_info) {
	struct drm_drm *drm = &ctx->drm;
	const uint32_t fb_id = get_framebuffer(drm, ctx->gbm.surface, wctx,
		&ctx->gbm.bo);
	if (!fb_id) {
		return false;
	}

	const int fail = drmModeSetCrtc(drm->fd, drm->crtc_id, fb_id, 0, 0,
		&drm->connector_id, 1, mode_info);
	if (fail) {
		return false;
	}
	return true;
}

static bool gbm_setup(struct drm_gbm *gbm, const int drm_fd,
drmModeModeInfo *mode_info) {
	gbm->device = gbm_create_device(drm_fd);
	gbm->surface = gbm_surface_create(gbm->device,
		mode_info->hdisplay, mode_info->vdisplay, WU_GBM_FORMAT,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	return (bool)gbm->surface;
}

static uint32_t find_crtc(const int fd, const drmModeRes *res,
const drmModeConnector *connector) {
	for (int i = 0; i < res->count_encoders; ++i) {
		drmModeEncoder *ec = drmModeGetEncoder(fd, res->encoders[i]);
		if (ec) {
			const uint32_t encoder_id = ec->encoder_id;
			const uint32_t crtc_id = ec->crtc_id;
			drmModeFreeEncoder(ec);
			if (encoder_id == connector->encoder_id) {
				return crtc_id;
			}
		}
	}

	for (int i = 0; i < connector->count_encoders; ++i) {
		drmModeEncoder *ec = drmModeGetEncoder(fd,
			connector->encoders[i]);
		if (ec) {
			const uint32_t possible_crtcs = ec->possible_crtcs;
			drmModeFreeEncoder(ec);

			for (int k = 0; k < res->count_crtcs; ++k) {
				if (possible_crtcs & (1 << k)) {
					if (res->crtcs[k]) {
						return res->crtcs[k];
					} else {
						break;
					}
				}
			}
		}
	}
	return 0;
}

static drmModeModeInfo * find_preferred_mode(drmModeConnector *connector) {
	drmModeModeInfo *mode_info = NULL;
	int greatest_area = 0;
	for (int i = 0; i < connector->count_modes; ++i) {
		drmModeModeInfo *mi = connector->modes + i;
		if (mi->type & DRM_MODE_TYPE_PREFERRED) {
			return mi;
		}

		const int mode_area = mi->hdisplay * mi->vdisplay;
		if (mode_area > greatest_area) {
			mode_info = mi;
			greatest_area = mode_area;
		}
	}
	return mode_info;
}

static drmModeConnector * find_connector(drmModeRes *res, const int fd) {
	drmModeConnector *unknown = NULL;
	for (int i = 0; i < res->count_connectors; ++i) {
		drmModeConnector *cn = drmModeGetConnector(fd,
			res->connectors[i]);
		switch (cn->connection) {
		case DRM_MODE_CONNECTED:
			if (unknown) {
				drmModeFreeConnector(unknown);
			}
			return cn;
		case DRM_MODE_UNKNOWNCONNECTION:
			if (!unknown) {
				unknown = cn;
				continue;
			}
			break;
		case DRM_MODE_DISCONNECTED:
			break;
		}
		drmModeFreeConnector(cn);
	}
	return unknown;
}

static int open_device(drmDevice *device) {
	if (device->available_nodes & (1 << DRM_NODE_PRIMARY)) {
		return open(device->nodes[DRM_NODE_PRIMARY], O_RDWR);
	}
	return -1;
}

static bool drm_setup_device(struct drm_drm *drm, const int fd, drmModeRes *res,
drmModeConnector **connector, drmModeModeInfo **mode_info) {
	*connector = find_connector(res, fd);
	if (*connector) {
		*mode_info = find_preferred_mode(*connector);
		if (*mode_info) {
			drm->crtc_id = find_crtc(fd, res, *connector);
			if (drm->crtc_id != 0) {
				drm->fd = fd;
				drm->connector_id = (*connector)->connector_id;
				return true;
			}
		}
	}
	return false;
}

static bool drm_setup(struct drm_drm *drm, drmModeConnector **connector,
drmModeModeInfo **mode_info) {
	drmDevice *devices[64];
	const int dev_len = drmGetDevices2(0, devices, ARRAY_LEN(devices));
	bool ok = false;
	for (int i = 0; i < dev_len; ++i) {
		const int fd = open_device(devices[i]);
		if (fd < 0) {
			continue;
		}

		drmModeRes *res = drmModeGetResources(fd);
		if (res) {
			ok = drm_setup_device(drm, fd, res, connector,
				mode_info);
			drmModeFreeResources(res);
			if (ok) {
				break;
			}
		}
		close(fd);
	}
	drmFreeDevices(devices, dev_len);
	return ok;
}

const char * drm_init(struct window_context *wctx) {
	struct drm_context *ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		return "Failed to allocate context memory";
	}
	ctx->drm.fd = -1;
	wctx->win.ctx = ctx;
	wctx->win.backend_name = "DRM";
	wctx->win.fn = (struct window_fn) {
		.title = null_function,
		.fullscreen = null_function,
		.resize = null_function,
		.poll = input_poll,
		.swap_buffers = drm_swap_buffers,
		.terminate = drm_terminate,
	};

	drmModeConnector *connector = NULL;
	drmModeModeInfo *mode_info = NULL; // ptr to a *connector member

	const char *err = input_setup(wctx, ctx);
	if (!err) {
		if (drm_setup(&ctx->drm, &connector, &mode_info)) {
			wctx->win.refresh_nsec = 1000000000 / mode_info->vrefresh;
			if (gbm_setup(&ctx->gbm, ctx->drm.fd, mode_info)) {
				err = egl_init(&ctx->egl,
					(EGLNativeDisplayType)ctx->gbm.device,
					ctx->gbm.surface, WU_GBM_FORMAT, false);
				if (!err) {
					if (mode_set(wctx, ctx, mode_info)) {
						// Phew
						err = NULL;
					} else {
						err = "Mode set failed";
					}
				} else {
					egl_print_error();
				}
			} else {
				err = "GBM setup failed";
			}
		} else {
			err = "DRM setup failed";
		}
	}
	drmModeFreeConnector(connector);
	return err;
}

static void drm_offscreen_terminate(struct window_offscreen *wctx) {
	struct drm_offscreen *ctx = wctx->ctx;
	egl_offscreen_terminate(ctx->egl_display);
	if (ctx->device) {
		gbm_device_destroy(ctx->device);
	}
	if (ctx->fd >= 0) {
		close(ctx->fd);
	}
}

static int get_device_fd(void) {
	drmDevice *devices[64];
	const int dev_len = drmGetDevices2(0, devices, ARRAY_LEN(devices));
	int fd = -1;
	for (int i = 0; fd < 0 && i < dev_len; ++i) {
		fd = open_device(devices[i]);
	}
	drmFreeDevices(devices, dev_len);
	return fd;
}

const char * drm_offscreen_init(struct window_offscreen *wctx) {
	struct drm_offscreen *ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		return "Failed to allocate context memory";
	}

	wctx->backend_name = "DRM offscreen";
	wctx->ctx = ctx;
	wctx->terminate = drm_offscreen_terminate;
	ctx->fd = get_device_fd();
	if (ctx->fd < 0) {
		return "No device found";
	}

	const char *err = NULL;
	ctx->device = gbm_create_device(ctx->fd);
	if (ctx->device) {
		err = egl_offscreen_init_direct(&ctx->egl_display, ctx->device);
	} else {
		err = "Failed to create GBM device.";
	}
	return err;
}
