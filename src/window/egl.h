#ifndef WU_EGL
#define WU_EGL

#include <epoxy/egl.h>

struct egl {
	EGLDisplay display;
	EGLSurface surface;
};

void egl_print_error(void);

bool egl_swap(const struct egl *egl);

bool egl_init(struct egl *egl, void *native_display, void *native_window,
uint32_t native_visual, bool transparent);

#endif /* WU_EGL */
