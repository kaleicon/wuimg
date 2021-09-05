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

#include "common.h"
#include "opengl.h"
#include "drm.h"

static const uint32_t WU_GBM_FORMAT = GBM_FORMAT_XRGB8888;

void kms_terminate(struct kms_context *ctx) {
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
struct display_dims *dims) {
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
			puts("not enough framebuffers :o");
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
		if (dims) {
			dims->w = width;
			dims->h = height;
		}
		gbm_bo_set_user_data(bo, fb_id, fb_destroy_fn);
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

void kms_swap_buffers(struct kms_context *ctx) {
	eglSwapBuffers(ctx->egl.display, ctx->egl.surface);

	struct gbm_bo *next_bo = gbm_surface_lock_front_buffer(ctx->gbm.surface);
	struct drm *drm = &ctx->drm;
	const uint32_t fb_id = get_framebuffer(drm, next_bo, NULL);
	if (!fb_id) {
		puts("failed to get framebuffer");
		return;
	}

	bool flipped = false;
	int status = drmModePageFlip(drm->fd, drm->crtc_id, fb_id,
		DRM_MODE_PAGE_FLIP_EVENT, &flipped);
	if (status) {
		puts("page flip failed");
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
			puts("DRM descriptor poll timed out after 1 second");
			break;
		}

		drmHandleEvent(drm->fd, &evctx);
	}

	gbm_surface_release_buffer(ctx->gbm.surface, ctx->gbm.bo);
	ctx->gbm.bo = next_bo;
}

static bool mode_set(struct kms_context *ctx, struct display_dims *dims,
drmModeModeInfo *mode_info) {
	eglSwapBuffers(ctx->egl.display, ctx->egl.surface);

	ctx->gbm.bo = gbm_surface_lock_front_buffer(ctx->gbm.surface);
	struct drm *drm = &ctx->drm;
	const uint32_t fb_id = get_framebuffer(drm, ctx->gbm.bo, dims);
	if (!fb_id || !dims->w || !dims->h) {
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

static bool egl_setup(struct egl *egl, const struct gbm *gbm) {
	if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
		puts("eglBindAPI fail");
		return false;
	}

	EGLint major, minor;
	egl->display = eglGetDisplay((EGLNativeDisplayType)gbm->device);
	if (eglInitialize(egl->display, &major, &minor) == EGL_FALSE) {
		puts("eglInitialize fail");
		return false;
	} else if (major != 1 || minor < 4) {
		printf("too old version: %d.%d\n", major, minor);
		return false;
	}

	const EGLint config_attrib[] = {
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE,
	};

	EGLConfig cfg[64];
	EGLint cnt = (EGLint)ARRAY_LEN(cfg);
	eglChooseConfig(egl->display, config_attrib, cfg, cnt, &cnt);
	if (cnt < 1) {
		puts("egl: no good config");
		return false;
	}

	EGLint context_attrib[9] = {
		EGL_CONTEXT_MAJOR_VERSION_KHR, WU_GL_MAJOR,
		EGL_CONTEXT_MINOR_VERSION_KHR, WU_GL_MINOR,
		EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
			EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
		EGL_NONE,
	};
	if (minor == 4) {
		context_attrib[6] = EGL_CONTEXT_FLAGS_KHR;
		context_attrib[7] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR;
	} else {
		context_attrib[6] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE;
		context_attrib[7] = EGL_TRUE;
	}
	context_attrib[8] = EGL_NONE;

	EGLContext context = EGL_NO_CONTEXT;
	int i = 0;
	while (i < cnt) {
		EGLint id;
		if (eglGetConfigAttrib(egl->display, cfg[i],
		EGL_NATIVE_VISUAL_ID, &id) == EGL_FALSE) {
			continue;
		}

		if ((uint32_t)id == WU_GBM_FORMAT) {
			context = eglCreateContext(egl->display, cfg[i],
				EGL_NO_CONTEXT, context_attrib);
			if (context != EGL_NO_CONTEXT) {
				break;
			}
		}
		++i;
	}

	if (context == EGL_NO_CONTEXT) {
		puts("egl: failed to create context");
		return false;
	}

	egl->surface = eglCreateWindowSurface(egl->display, cfg[i],
		(EGLNativeWindowType)gbm->surface, NULL);
	if (egl->surface == EGL_NO_SURFACE) {
		puts("eglCreateWindowSurface fail");
		return false;
	}

	if (eglMakeCurrent(egl->display, egl->surface, egl->surface, context)
	== EGL_FALSE) {
		puts("eglMakeCurrent fail");
		return false;
	}
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

	for (int i = 0; i < res->count_connectors; ++i) {
		drmModeConnector *cn = drmModeGetConnector(drm->fd,
			res->connectors[i]);
		if (cn->connection == DRM_MODE_CONNECTED) {
			*connector = cn;
			break;
		}
		drmModeFreeConnector(cn);
	}
	if (!*connector) {
		drmModeFreeResources(res);
		return false;
	}

	int area = 0;
	for (int i = 0; i < (*connector)->count_modes; ++i) {
		drmModeModeInfo *mi = (*connector)->modes + i;
		if (mi->type & DRM_MODE_TYPE_PREFERRED) {
			*mode_info = mi;
			break;
		}

		const int mode_area = mi->hdisplay * mi->vdisplay;
		if (mode_area > area) {
			*mode_info = mi;
			area = mode_area;
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

bool kms_setup(struct kms_context *ctx, struct display_dims *dims) {
	*ctx = (struct kms_context){0};
	ctx->drm.fd = -1;

	// mode_info is a pointer to a connector member
	drmModeConnector *connector = NULL;
	drmModeModeInfo *mode_info = NULL;

	bool is_ok = false;
	if (drm_setup(&ctx->drm, &connector, &mode_info)) {
		if (gbm_setup(&ctx->gbm, ctx->drm.fd, mode_info)) {
			if (egl_setup(&ctx->egl, &ctx->gbm)) {
				if (mode_set(ctx, dims, mode_info)) {
					// Phew
					is_ok = true;
				} else {
					puts("Mode set failed");
				}
			} else {
				fputs("EGL setup fail", stdout);
				const EGLint error = eglGetError();
				if (error != EGL_SUCCESS) {
					printf(": %#x", (unsigned)error);
				}
				putchar('\n');
			}
		} else {
			puts("GBM setup fail");
		}
	} else {
		puts("DRM setup fail");
	}
	drmModeFreeConnector(connector);

	if (!is_ok) {
		kms_terminate(ctx);
		*ctx = (struct kms_context){0};
		ctx->drm.fd = -1;
	}
	return is_ok;
}
