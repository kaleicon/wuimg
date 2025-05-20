// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef GRAPHICS_ADAPTERS
#define GRAPHICS_ADAPTERS

#include <stddef.h>

#include "raster/strip.h"

void bitplane_interleave_row8(uint8_t *restrict dst, const uint8_t *restrict src,
size_t w, uint8_t planes, align_t align);

void bitplane_interleave_row(void *restrict dst, const uint8_t *restrict src,
size_t w, uint8_t planes, align_t align);

void bitplane_interleave_plane(uint8_t *restrict dst,
const uint8_t *restrict src, size_t w, uint8_t planes, align_t align, size_t h);

void bitplane_interleave_pack(uint8_t *restrict dst,
const uint8_t *restrict src, size_t w, uint8_t planes, align_t align);

void v9958_ykj_to_grb(upack1555_t *dst, const uint8_t *restrict src,
size_t dwords, const struct palette *yae);

struct pix_rgba8 ega_palette(size_t idx);

struct pix_rgba8 cga_palette(size_t idx);

#endif /* GRAPHICS_ADAPTERS */
