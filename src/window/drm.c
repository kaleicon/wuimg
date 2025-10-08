// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

#include <xf86drm.h>
#include <gbm.h>

#include "misc/math.h"
#include "misc/term.h"
#include "window/drm.h"
#include "opengl.h"

static const uint32_t WU_GBM_FORMAT = GBM_FORMAT_XRGB8888;

static void drm_terminate(void *ctxv) {
	struct drm_context *ctx = ctxv;
	eglTerminate(ctx->egl.display);

	if (ctx->drm.fd != -1) {
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

	drmModeCrtc *crtc = ctx->drm.crtc_restore;
	if (crtc) {
		drmModeSetCrtc(ctx->drm.fd, crtc->crtc_id, crtc->buffer_id,
			0, 0, &ctx->drm.connector_id, 1, &crtc->mode);
		drmModeFreeCrtc(crtc);
	}

	if (ctx->drm.fd != -1) {
		close(ctx->drm.fd);
	}
}

static void fb_destroy_fn(struct gbm_bo *bo, void *data) {
	uint32_t *fb_ptr = data;
	drmModeRmFB(gbm_bo_get_fd(bo), *fb_ptr);
	*fb_ptr = 0;
}

static uint32_t get_framebuffer(struct drm_drm *drm,
struct gbm_surface *surface, struct window_public *pub, struct gbm_bo **bo) {
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
		window_size_update(pub, (int)width, (int)height);
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

static void drm_swap_buffers(void *ctxv) {
	struct drm_context *ctx = ctxv;
	egl_swap(&ctx->egl);

	struct drm_drm *drm = &ctx->drm;
	struct gbm_bo *next_bo;
	const uint32_t fb_id = get_framebuffer(drm, ctx->gbm.surface, ctx->pub,
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

static bool mode_set(struct drm_context *ctx, drmModeModeInfo *mode_info) {
	struct drm_drm *drm = &ctx->drm;
	const uint32_t fb_id = get_framebuffer(drm, ctx->gbm.surface, ctx->pub,
		&ctx->gbm.bo);
	if (!fb_id) {
		return false;
	}

	drmModeCrtc *crtc_restore = drmModeGetCrtc(drm->fd, drm->crtc_id);
	if (!crtc_restore) {
		return false;
	}

	const int fail = drmModeSetCrtc(drm->fd, drm->crtc_id, fb_id, 0, 0,
		&drm->connector_id, 1, mode_info);
	if (fail) {
		drmModeFreeCrtc(crtc_restore);
		return false;
	}
	drm->crtc_restore = crtc_restore;
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

static drmModeRes * find_primary_device(struct drm_drm *drm) {
	drmDevice *devices[64];
	const int dev_len = drmGetDevices2(0, devices, ARRAY_LEN(devices));
	drmModeRes *res = NULL;
	for (int i = 0; i < dev_len; ++i) {
		const int fd = open_device(devices[i]);
		if (fd >= 0) {
			res = drmModeGetResources(fd);
			if (res) {
				drm->fd = fd;
				break;
			}
			close(fd);
		}
	}
	drmFreeDevices(devices, dev_len);
	return res;
}

static bool drm_setup(struct drm_drm *drm, drmModeConnector **connector,
drmModeModeInfo **mode_info) {
	drmModeRes *res = find_primary_device(drm);
	if (res) {
		*connector = find_connector(res, drm->fd);
		if (*connector) {
			*mode_info = find_preferred_mode(*connector);
			if (*mode_info) {
				drm->connector_id = (*connector)->connector_id;
				drm->crtc_id = find_crtc(drm->fd, res, *connector);
			}
		}
		drmModeFreeResources(res);
	}
	return drm->crtc_id != 0;
}

const char * drm_init(struct drm_context *ctx, struct window_public *pub) {
	*ctx = (struct drm_context){
		.pub = pub,
		.drm.fd = -1,
	};
	pub->win.fn = (struct window_fn) {
		.title = null_function,
		.fullscreen = null_function,
		.resize = null_function,
		.poll = null_function,
		.swap_buffers = drm_swap_buffers,
		.terminate = drm_terminate,
	};

	drmModeConnector *connector = NULL;
	drmModeModeInfo *mode_info = NULL; // ptr to a *connector member

	const char *err = "This string shouldn't be seen";
	if (drm_setup(&ctx->drm, &connector, &mode_info)) {
		pub->win.refresh_nsec = 1000000000 / mode_info->vrefresh;
		if (gbm_setup(&ctx->gbm, ctx->drm.fd, mode_info)) {
			err = egl_init(&ctx->egl,
				(EGLNativeDisplayType)ctx->gbm.device,
				ctx->gbm.surface, WU_GBM_FORMAT, false);
			if (!err) {
				if (mode_set(ctx, mode_info)) {
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
	drmModeFreeConnector(connector);
	return err;
}

static void drm_offscreen_terminate(void *ctxv) {
	struct drm_offscreen *ctx = ctxv;
	egl_offscreen_terminate(ctx->egl_display);
	if (ctx->device) {
		gbm_device_destroy(ctx->device);
	}
	if (ctx->fd >= 0) {
		close(ctx->fd);
	}
}

const char * drm_offscreen_init(struct drm_offscreen *ctx,
window_fn_ctx_t *terminate) {
	*ctx = (struct drm_offscreen){0};

	ctx->fd = get_device_fd();
	if (ctx->fd < 0) {
		return "No device found";
	}

	const char *err = NULL;
	ctx->device = gbm_create_device(ctx->fd);
	if (ctx->device) {
		err = egl_offscreen_init(&ctx->egl_display, NULL, ctx->device);
	} else {
		err = "Failed to create GBM device.";
	}
	if (err) {
		drm_offscreen_terminate(ctx);
	} else {
		*terminate = drm_offscreen_terminate;
	}
	return err;
}
