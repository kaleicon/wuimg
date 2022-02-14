#ifndef GRAPHICS_ADAPTERS
#define GRAPHICS_ADAPTERS

#include <stddef.h>

#include "raster.h"

void vga_interleave(uint8_t *restrict dst, const uint8_t *restrict src,
size_t w, size_t h, uint8_t ch, uint8_t bitdepth, bool paletted);

struct pix_rgba8 ega_palette(size_t idx);

struct pix_rgba8 cga_palette(size_t idx);

#endif /* GRAPHICS_ADAPTERS */
