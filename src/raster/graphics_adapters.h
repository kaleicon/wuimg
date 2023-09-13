// SPDX-License-Identifier: 0BSD
#ifndef GRAPHICS_ADAPTERS
#define GRAPHICS_ADAPTERS

#include <stddef.h>

#include "raster/strip.h"

void vga_interleave(uint8_t *restrict dst, const uint8_t *restrict src,
size_t w, size_t h, uint8_t ch, uint8_t bitdepth, align_t align, bool paletted);

void v9958_ykj_to_grb(upack1555_t *dst, const uint8_t *restrict src,
size_t dwords, const struct raster_pal *yae);

struct pix_rgba8 ega_palette(size_t idx);

struct pix_rgba8 cga_palette(size_t idx);

#endif /* GRAPHICS_ADAPTERS */
