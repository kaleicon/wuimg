#include "egl.h"

const char * offscree_init(struct offscreen *os) {
	if (!egl_surfaceless_init(&os->egl, EGL_DEFAULT_DISPLAY,
}
