#include <epoxy/egl.h>

#include "egl.h"
// WU_GL_MAJOR|MINOR macros
#include "opengl.h"

void egl_print_error(void) {
	const EGLint error = eglGetError();
	if (error != EGL_SUCCESS) {
		fprintf(stderr, "egl error %#x\n", (unsigned)error);
	}
}

bool egl_swap(const struct egl *egl) {
	return eglSwapBuffers(egl->display, egl->surface);
}

EGLint egl_init_common(struct egl *egl, void *native_display,
const EGLint *restrict cfg_attrib, EGLConfig *cfg, EGLint *restrict cnt) {
	if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE) {
		fputs("eglBindAPI fail\n", stderr);
		return -1;
	}

	EGLint major, minor;
	egl->display = eglGetDisplay(native_display);
	if (eglInitialize(egl->display, &major, &minor) != EGL_TRUE) {
		fputs("eglInitialize fail\n", stderr);
		return -1;
	} else if (major != 1 || minor < 4) {
		fprintf(stderr, "egl version too old: %d.%d\n", major, minor);
		return -1;
	}

	eglChooseConfig(egl->display, cfg_attrib, cfg, *cnt, cnt);
	if (*cnt < 1) {
		fputs("egl: no good config\n", stderr);
		return -1;
	}
	return minor;
}

bool egl_surfaceless_init(struct egl *egl) {
	EGLConfig cfg[1];
	EGLint cnt = (EGLint)ARRAY_LEN(cfg);
	if (egl_init_common(egl, EGL_DEFAULT_DISPLAY, NULL, cfg, &cnt) == -1) {
		return false;
	}
	return true;
}

bool egl_init(struct egl *egl, void *native_display, void *native_window,
const uint32_t native_visual, const bool transparent) {
	const EGLint cfg_attrib[] = {
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, (transparent) ? 1 : 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE,
	};
	EGLConfig cfg[64];
	EGLint cnt = (EGLint)ARRAY_LEN(cfg);
	EGLint minor = egl_init_common(egl, native_display, cfg_attrib, cfg, &cnt);
	if (minor == -1) {
		return false;
	}

	EGLint context_attrib[9] = {
		EGL_CONTEXT_MAJOR_VERSION_KHR, WU_GL_MAJOR,
		EGL_CONTEXT_MINOR_VERSION_KHR, WU_GL_MINOR,
		EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
			EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
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
			EGL_NO_CONTEXT, context_attrib);
		if (context != EGL_NO_CONTEXT) {
			break;
		}
		++i;
	}

	if (context == EGL_NO_CONTEXT) {
		fputs("egl: failed to create context\n", stderr);
		return false;
	}

	egl->surface = eglCreateWindowSurface(egl->display, cfg[i],
		(EGLNativeWindowType)native_window, NULL);
	if (egl->surface == EGL_NO_SURFACE) {
		fputs("eglCreateWindowSurface fail\n", stderr);
		return false;
	}

	if (eglMakeCurrent(egl->display, egl->surface, egl->surface, context)
	!= EGL_TRUE) {
		fputs("eglMakeCurrent fail\n", stderr);
		return false;
	}
	return true;
}
