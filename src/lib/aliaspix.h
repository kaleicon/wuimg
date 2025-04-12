// SPDX-License-Identifier: 0BSD
#ifndef LIB_ALIASPIX
#define LIB_ALIASPIX

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct aliaspix_desc {
	struct mparser mp;
	uint16_t x, y;
};

struct wu_st aliaspix_decode(const struct aliaspix_desc *desc,
struct wuimg *img);

struct wu_st aliaspix_init(struct aliaspix_desc *desc, struct wuimg *img,
struct wuptr mem);

#endif /* LIB_ALIASPIX */
