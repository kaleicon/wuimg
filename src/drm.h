#ifndef DRM_BACKEND
#define DRM_BACKEND

#include <epoxy/egl.h>
#include <libdrm/drm_mode.h>
#include <xf86drmMode.h>

#include "common.h"

struct drm_context {
	struct drm {
		drmModeCrtc *crtc_restore;
		uint32_t connector_id;
		uint32_t crtc_id;
		uint32_t fb_id[2];
		int fd;
	} drm;

	struct gbm {
		struct gbm_device *device;
		struct gbm_surface *surface;
		struct gbm_bo *bo;
	} gbm;

	struct egl {
		EGLDisplay display;
		EGLSurface surface;
	} egl;
};

void drm_terminate(struct drm_context *ctx);

void drm_swap_buffers(struct drm_context *ctx);

bool drm_init(struct drm_context *ctx, struct display_dims *dims);

#endif /* DRM_BACKEND */
