// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef WU_EGL
#define WU_EGL

#include <epoxy/egl.h>

#include "base.h"

struct egl {
	EGLDisplay display;
	EGLSurface surface;
};

void egl_print_error(void);

void egl_offscreen_terminate(EGLDisplay display);

const char * egl_offscreen_init_direct(EGLDisplay *display,
EGLNativeDisplayType native_display);

const char * egl_offscreen_init(struct window_offscreen *ctx,
EGLNativeDisplayType native_display);

void egl_terminate(struct egl *egl);

bool egl_swap(const struct egl *egl);

const char * egl_init(struct egl *egl, EGLNativeDisplayType native_display,
void *native_window, uint32_t native_visual, bool transparent);

#endif /* WU_EGL */
