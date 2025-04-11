// SPDX-License-Identifier: 0BSD
#ifndef LIB_ECLIPSE
#define LIB_ECLIPSE

#include "raster/wuimg.h"

enum eclipse_colorspace {
	eclipse_rgb = 0,
	eclipse_cmyk = 1,
};

struct eclipse_desc {
	FILE *ifp;
	char software[32];
	char revision[32];
	uint32_t w, h;
	enum eclipse_colorspace colorspace;
};

struct wu_st eclipse_load(struct eclipse_desc *desc, struct wuimg *img);

struct wu_st eclipse_init(struct eclipse_desc *desc, struct wuimg *img,
FILE *ifp);

#endif /* LIB_ECLIPSE */
