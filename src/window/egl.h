// SPDX-License-Identifier: 0BSD
#ifndef WU_EGL
#define WU_EGL

#include <epoxy/egl.h>

#include "base.h"

struct egl {
	EGLDisplay display;
	EGLSurface surface;
};

void egl_print_error(void);

void egl_offscreen_terminate(void *ctx);

const char * egl_offscreen_init(EGLDisplay *display, window_fn_ctx_t *terminate,
EGLNativeDisplayType native_display);

void egl_terminate(struct egl *egl);

bool egl_swap(const struct egl *egl);

const char * egl_init(struct egl *egl, EGLNativeDisplayType native_display,
void *native_window, uint32_t native_visual, bool transparent);

#endif /* WU_EGL */
