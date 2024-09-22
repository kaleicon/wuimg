// SPDX-License-Identifier: 0BSD
#ifndef RASTER_PAL
#define RASTER_PAL

#include "pix.h"

struct raster_pal {
	struct pix_rgba8 color[256];
};

void raster_pal_print(const struct raster_pal *cm);

void raster_pal_cyclecopy(struct raster_pal *restrict dst,
const struct raster_pal *restrict src, const size_t base, const size_t i,
const size_t cnt);

void raster_pal_expand(void *restrict dst, const uint8_t *restrict src,
const struct raster_pal *cm, size_t width, uint8_t bitdepth);

void raster_pal_from_rgb8(struct raster_pal *dst, const void *src, size_t nmemb);

#endif /* RASTER_PAL */
