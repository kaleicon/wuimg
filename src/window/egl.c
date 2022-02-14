#include <epoxy/egl.h>

#include "egl.h"
#include "opengl.h"

static const char CREATE_CONTEXT_FAIL[] = "EGL: Failed to create context";

void egl_print_error(void) {
	const EGLint error = eglGetError();
	if (error != EGL_SUCCESS) {
		fprintf(stderr, "egl error %#x\n", (unsigned)error);
	}
}

static const char * egl_make_current(EGLDisplay display, EGLSurface surface,
EGLContext context) {
	if (eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
		return "EGL: Couldn't make context current";
	}
	if (context != EGL_NO_CONTEXT) {
		eglSwapInterval(display, 0);
	}
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
EGLConfig *cfg, EGLint *restrict cfg_cnt, EGLint ctx_attr[static 9]) {
	EGLint major, minor;
	*display = eglGetDisplay(native_display);
	if (*display == EGL_NO_DISPLAY) {
		return "EGL: No matching display";
	} else if (eglInitialize(*display, &major, &minor) != EGL_TRUE) {
		return "EGL: eglInitializate failed";
	} else if (major != 1 || minor < 4) {
		return "EGL: EGL version too old, 1.4+ required";
	}

	if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE) {
		return "EGL: Couldn't bind to OpenGL API";
	}

	eglChooseConfig(*display, cfg_attr, cfg, *cfg_cnt, cfg_cnt);
	if (*cfg_cnt < 1) {
		return "EGL: No config found";
	}

	ctx_attr[0] = EGL_CONTEXT_MAJOR_VERSION_KHR;
	ctx_attr[1] = WU_GL_MAJOR;

	ctx_attr[2] = EGL_CONTEXT_MINOR_VERSION_KHR;
	ctx_attr[3] = WU_GL_MINOR;

	ctx_attr[4] = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
	ctx_attr[5] = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR;

	if (minor == 4) {
		ctx_attr[6] = EGL_CONTEXT_FLAGS_KHR;
		ctx_attr[7] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR;
	} else {
		ctx_attr[6] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE;
		ctx_attr[7] = EGL_TRUE;
	}
	ctx_attr[8] = EGL_NONE;
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
	EGLint ctx_attr[9];

	const char *err = egl_init_common(display, native_display, cfg_attr,
		cfg, &cfg_cnt, ctx_attr);
	if (err) {
		return err;
	}

	EGLContext context = eglCreateContext(*display, *cfg, EGL_NO_CONTEXT,
		ctx_attr);
	if (context == EGL_NO_CONTEXT) {
		return CREATE_CONTEXT_FAIL;
	}
	return egl_make_current(*display, EGL_NO_SURFACE, context);
}

const char * egl_init(struct egl *egl, EGLNativeDisplayType native_display,
void *native_window, const uint32_t native_visual, const bool transparent) {
	const EGLint cfg_attr[] = {
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, (transparent) ? 1 : 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE,
	};
	EGLConfig cfg[64];
	EGLint cfg_cnt = (EGLint)ARRAY_LEN(cfg);
	EGLint ctx_attr[9];

	const char *err = egl_init_common(&egl->display, native_display,
		cfg_attr, cfg, &cfg_cnt, ctx_attr);
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
			EGL_NO_CONTEXT, ctx_attr);
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
		return "egl: Failed to create window surface";
	}
	return egl_make_current(egl->display, egl->surface, context);
}
