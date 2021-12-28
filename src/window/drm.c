#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

#include <epoxy/egl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

#include "../common.h"
#include "../opengl.h"
#include "drm.h"

static const uint32_t WU_GBM_FORMAT = GBM_FORMAT_XRGB8888;

void drm_terminate(struct drm_context *ctx) {
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
	uint32_t *fb_id = data;
	drmModeRmFB(gbm_bo_get_fd(bo), *fb_id);
	*fb_id = 0;
}

static uint32_t get_framebuffer(struct drm *drm, struct gbm_bo *bo,
struct window_public *pub) {
	uint32_t *fb_id = gbm_bo_get_user_data(bo);
	if (fb_id) {
		if (*fb_id) {
			return *fb_id;
		}
	} else {
		for (size_t i = 0; i < ARRAY_LEN(drm->fb_id); ++i) {
			if (!drm->fb_id[i]) {
				fb_id = drm->fb_id + i;
				break;
			}
		}
		if (fb_id == NULL) {
			fputs("not enough framebuffers :o\n", stderr);
			return 0;
		}
	}

	const uint32_t width = gbm_bo_get_width(bo);
	const uint32_t height = gbm_bo_get_height(bo);
	const uint32_t format = gbm_bo_get_format(bo);

	uint32_t handles[4] = {gbm_bo_get_handle(bo).u32, 0};
	uint32_t pitches[4] = {gbm_bo_get_stride(bo), 0};
	uint32_t offsets[4] = {0};

	const int fail = drmModeAddFB2(drm->fd, width, height, format, handles,
		pitches, offsets, fb_id, 0);
	if (fail) {
		*fb_id = 0;
	} else {
		gbm_bo_set_user_data(bo, fb_id, fb_destroy_fn);
		window_size_update(pub, width, height);
	}
	return *fb_id;
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

void drm_swap_buffers(struct drm_context *ctx) {
	egl_swap(&ctx->egl);

	struct gbm_bo *next_bo = gbm_surface_lock_front_buffer(ctx->gbm.surface);
	struct drm *drm = &ctx->drm;
	const uint32_t fb_id = get_framebuffer(drm, next_bo, ctx->pub);
	if (!fb_id) {
		fputs("failed to get framebuffer\n", stderr);
		return;
	}

	bool flipped = false;
	int status = drmModePageFlip(drm->fd, drm->crtc_id, fb_id,
		DRM_MODE_PAGE_FLIP_EVENT, &flipped);
	if (status) {
		fputs("page flip failed\n", stderr);
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
			fputs("DRM descriptor poll timed out after 1 second\n",
				stderr);
			break;
		}

		drmHandleEvent(drm->fd, &evctx);
	}

	gbm_surface_release_buffer(ctx->gbm.surface, ctx->gbm.bo);
	ctx->gbm.bo = next_bo;
}

static bool mode_set(struct drm_context *ctx, drmModeModeInfo *mode_info) {
	egl_swap(&ctx->egl);

	ctx->gbm.bo = gbm_surface_lock_front_buffer(ctx->gbm.surface);
	struct drm *drm = &ctx->drm;
	const uint32_t fb_id = get_framebuffer(drm, ctx->gbm.bo, ctx->pub);
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

static bool gbm_setup(struct gbm *gbm, const int drm_fd,
drmModeModeInfo *mode_info) {
	gbm->device = gbm_create_device(drm_fd);
	gbm->surface = gbm_surface_create(gbm->device,
		mode_info->hdisplay, mode_info->vdisplay, WU_GBM_FORMAT,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	return gbm->surface != NULL;
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

			uint32_t crtc_id = 0;
			for (int k = 0; k < res->count_crtcs; ++k) {
				if (possible_crtcs & (1 << k)) {
					crtc_id = res->crtcs[k];
					break;
				}
			}

			if (crtc_id) {
				return crtc_id;
			}
		}
	}
	return 0;
}

static drmModeConnector * find_connector(drmModeRes *res, const int fd) {
	drmModeConnector *unknown = NULL;
	for (int i = 0; i < res->count_connectors; ++i) {
		drmModeConnector *cn = drmModeGetConnector(fd,
			res->connectors[i]);
		switch (cn->connection) {
		case DRM_MODE_CONNECTED:
			if (unknown) {
				drmModeFreeConnector(cn);
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

static drmModeRes * find_device(struct drm *drm) {
	drmDevice *devices[64];
	const int dev_len = drmGetDevices2(0, devices, ARRAY_LEN(devices));
	if (dev_len < 1) {
		return NULL;
	}

	drmModeRes *res = NULL;
	for (int i = 0; i < dev_len; ++i) {
		drmDevice *device = devices[i];
		const bool is_primary = device->available_nodes
			& (1 << DRM_NODE_PRIMARY);
		if (!is_primary) {
			continue;
		}

		const int fd = open(device->nodes[DRM_NODE_PRIMARY], O_RDWR);
		if (fd < 0) {
			continue;
		}
		res = drmModeGetResources(fd);
		if (res) {
			drm->fd = fd;
			break;
		}
		close(fd);
	}
	drmFreeDevices(devices, dev_len);
	return res;
}

static bool drm_setup(struct drm *drm, drmModeConnector **connector,
drmModeModeInfo **mode_info) {
	drmModeRes *res = find_device(drm);
	if (!res) {
		return false;
	}

	*connector = find_connector(res, drm->fd);
	if (!*connector) {
		drmModeFreeResources(res);
		return false;
	}

	int max_area = 0;
	for (int i = 0; i < (*connector)->count_modes; ++i) {
		drmModeModeInfo *mi = (*connector)->modes + i;
		if (mi->type & DRM_MODE_TYPE_PREFERRED) {
			*mode_info = mi;
			break;
		}

		const int mode_area = mi->hdisplay * mi->vdisplay;
		if (mode_area > max_area) {
			*mode_info = mi;
			max_area = mode_area;
		}
	}
	if (!*mode_info) {
		drmModeFreeResources(res);
		return false;
	}

	drm->connector_id = (*connector)->connector_id;
	drm->crtc_id = find_crtc(drm->fd, res, *connector);
	drmModeFreeResources(res);
	return drm->crtc_id != 0;
}

const char * drm_init(struct drm_context *ctx, struct window_public *pub) {
	ctx->pub = pub;
	ctx->drm.fd = -1;

	drmModeConnector *connector = NULL;
	drmModeModeInfo *mode_info = NULL; // will point to a *connector member

	const char *err = "Undefined behaviour in DRM";
	if (drm_setup(&ctx->drm, &connector, &mode_info)) {
		if (gbm_setup(&ctx->gbm, ctx->drm.fd, mode_info)) {
			if (egl_init(&ctx->egl, ctx->gbm.device, ctx->gbm.surface, WU_GBM_FORMAT, false)) {
				if (mode_set(ctx, mode_info)) {
					// Phew
					err = NULL;
				} else {
					err = "Mode set failed";
				}
			} else {
				egl_print_error();
				err = "EGL setup failed";
			}
		} else {
			err = "GBM setup failed";
		}
	} else {
		err = "DRM setup failed";
	}
	drmModeFreeConnector(connector);

	if (err) {
		drm_terminate(ctx);
	}
	return NULL;
}
