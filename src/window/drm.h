// SPDX-License-Identifier: 0BSD
#ifndef DRM_BACKEND
#define DRM_BACKEND

#include <epoxy/egl.h>
#include <libdrm/drm_mode.h>
#include <xf86drmMode.h>

#include "base.h"
#include "egl.h"

struct drm_context {
	struct window_public *pub;

	struct drm_drm {
		drmModeCrtc *crtc_restore;
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
};

struct drm_offscreen {
	int fd;
	struct gbm_device *device;
	EGLDisplay egl_display;
};

const char * drm_init(struct drm_context *ctx, struct window_public *pub);

const char * drm_offscreen_init(struct drm_offscreen *ctx,
window_fn_ctx_t *terminate);

#endif /* DRM_BACKEND */
