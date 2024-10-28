// SPDX-License-Identifier: 0BSD
#ifndef RASTER_PAL
#define RASTER_PAL

#include "pix.h"

struct palette {
	uint32_t refs;
	struct pix_rgba8 color[256];
};

void palette_print(const struct palette *cm);

void palette_unref(struct palette *pal);

struct palette * palette_ref(struct palette *cm);

struct palette * palette_copy(struct palette *cm);

struct palette * palette_new(void);

void palette_cyclecopy(struct palette *restrict dst,
const struct palette *restrict src, const size_t base, const size_t i,
const size_t cnt);

void palette_expand(void *restrict dst, const uint8_t *restrict src,
const struct palette *cm, size_t width, uint8_t bitdepth);

void palette_from_rgb8(struct palette *dst, const void *src, size_t nmemb);

#endif /* RASTER_PAL */
