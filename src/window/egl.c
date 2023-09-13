// SPDX-License-Identifier: 0BSD
#include <epoxy/egl.h>

#include "misc/common.h"
#include "egl.h"
#include "opengl.h"

static const char CREATE_CONTEXT_FAIL[] = "Failed to create context";

struct ctx_attr {
	EGLint v[9];
};

static const char * egl_error_str(const EGLint error) {
	switch (error) {
	case EGL_SUCCESS: return "Success";
	case EGL_NOT_INITIALIZED: return "Not initialized";
	case EGL_BAD_ACCESS: return "Bad access";
	case EGL_BAD_ALLOC: return "Bad alloc";
	case EGL_BAD_ATTRIBUTE: return "Bad attribute";
	case EGL_BAD_CONFIG: return "Bad config";
	case EGL_BAD_CONTEXT: return "Bad context";
	case EGL_BAD_CURRENT_SURFACE: return "Bad current surface";
	case EGL_BAD_DISPLAY: return "Bad display";
	case EGL_BAD_MATCH: return "Bad match";
	case EGL_BAD_NATIVE_PIXMAP: return "Bad native pixmap";
	case EGL_BAD_NATIVE_WINDOW: return "Bad native window";
	case EGL_BAD_PARAMETER: return "Bad parameter";
	case EGL_BAD_SURFACE: return "Bad surface";
	case EGL_CONTEXT_LOST: return "Context lost";
	}
	return "???";
}

void egl_print_error(void) {
	const EGLint error = eglGetError();
	if (error != EGL_SUCCESS) {
		fprintf(stderr, "EGL error %#x: %s\n", (unsigned)error,
			egl_error_str(error));
	}
}

static const char * egl_make_current(EGLDisplay display, EGLSurface surface,
EGLContext context) {
	if (eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
		return "Couldn't make context current";
	}
//	if (context != EGL_NO_CONTEXT) {
		eglSwapInterval(display, 1);
//	}
	return NULL;
}

void egl_offscreen_terminate(EGLDisplay display) {
	egl_make_current(display, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglTerminate(display);
}

void egl_terminate(struct egl *egl) {
	egl_offscreen_terminate(egl->display);
}

bool egl_swap(const struct egl *egl) {
	return eglSwapBuffers(egl->display, egl->surface);
}

static const char * egl_init_common(EGLDisplay *display,
EGLNativeDisplayType native_display, const EGLint *restrict cfg_attr,
EGLConfig *cfg, EGLint *restrict cfg_cnt, struct ctx_attr *attr) {
	EGLint major, minor;
	*display = eglGetDisplay(native_display);
	if (*display == EGL_NO_DISPLAY) {
		return "No matching display";
	} else if (eglInitialize(*display, &major, &minor) != EGL_TRUE) {
		return "Couldn't initialized EGL";
	} else if (major != 1 || minor < 4) {
		return "Version too old, 1.4 <= required";
	}

	if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE) {
		return "Couldn't bind OpenGL API";
	}

	eglChooseConfig(*display, cfg_attr, cfg, *cfg_cnt, cfg_cnt);
	if (*cfg_cnt < 1) {
		return "No config candidates found";
	}

	attr->v[0] = EGL_CONTEXT_MAJOR_VERSION_KHR;
	attr->v[1] = WU_GL_MAJOR;

	attr->v[2] = EGL_CONTEXT_MINOR_VERSION_KHR;
	attr->v[3] = WU_GL_MINOR;

	attr->v[4] = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
	attr->v[5] = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR;

	if (minor == 4) {
		attr->v[6] = EGL_CONTEXT_FLAGS_KHR;
		attr->v[7] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR;
	} else {
		attr->v[6] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE;
		attr->v[7] = EGL_TRUE;
	}
	attr->v[8] = EGL_NONE;
	return NULL;
}

const char * egl_offscreen_init(EGLDisplay *display,
EGLNativeDisplayType native_display) {
	const EGLint cfg_attr[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE,
	};
	EGLConfig cfg[1];
	EGLint cfg_cnt = (EGLint)ARRAY_LEN(cfg);
	struct ctx_attr attr;

	const char *err = egl_init_common(display, native_display, cfg_attr,
		cfg, &cfg_cnt, &attr);
	if (err) {
		return err;
	}

	EGLContext context = eglCreateContext(*display, *cfg, EGL_NO_CONTEXT,
		attr.v);
	if (context == EGL_NO_CONTEXT) {
		return CREATE_CONTEXT_FAIL;
	}
	return egl_make_current(*display, EGL_NO_SURFACE, context);
}

const char * egl_init(struct egl *egl, EGLNativeDisplayType native_display,
void *native_window, const uint32_t native_visual, const bool transparent) {
	const EGLint cfg_attr[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, (transparent) ? 8 : 0,
		EGL_NONE,
	};
	EGLConfig cfg[32];
	EGLint cfg_cnt = (EGLint)ARRAY_LEN(cfg);
	struct ctx_attr attr;

	const char *err = egl_init_common(&egl->display, native_display,
		cfg_attr, cfg, &cfg_cnt, &attr);
	if (err) {
		return err;
	}

	EGLContext context = EGL_NO_CONTEXT;
	int i = 0;
	for (; i < cfg_cnt; ++i) {
		if (native_visual) {
			EGLint id;
			if (eglGetConfigAttrib(egl->display, cfg[i],
			EGL_NATIVE_VISUAL_ID, &id) != EGL_TRUE) {
				continue;
			}
			if ((uint32_t)id != native_visual) {
				continue;
			}
		}
		context = eglCreateContext(egl->display, cfg[i],
			EGL_NO_CONTEXT, attr.v);
		if (context != EGL_NO_CONTEXT) {
			break;
		}
	}
	if (context == EGL_NO_CONTEXT) {
		return CREATE_CONTEXT_FAIL;
	}

	const EGLint surf_attr[] = {
		EGL_RENDER_BUFFER, EGL_SINGLE_BUFFER,
		EGL_NONE,
	};

	egl->surface = eglCreateWindowSurface(egl->display, cfg[i],
		(EGLNativeWindowType)native_window, surf_attr);
	if (egl->surface == EGL_NO_SURFACE) {
		return "Failed to create window surface";
	}
	const char *msg = egl_make_current(egl->display, egl->surface, context);
	if (!msg) {
		return egl_swap(egl) ? NULL : "Failed to swap buffers";
	}
	return msg;
}
