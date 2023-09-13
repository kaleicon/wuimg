// SPDX-License-Identifier: 0BSD
#ifndef LIB_TIM
#define LIB_TIM

#include <stdio.h>

#include "raster/wuimg.h"

struct tim_clut {
	size_t nb;
	unsigned x, y;
};

struct tim_desc {
	FILE *ifp;
	unsigned x, y;
	struct tim_clut clut;
};

size_t tim_decode(const struct tim_desc *desc, struct wuimg *img);

enum wu_error tim_parse_header(struct tim_desc *desc, struct wuimg *img);

enum wu_error tim_open_file(struct tim_desc *desc, FILE *ifp);

#endif /* LIB_TIM */
