#ifndef GRAPHICS_ADAPTERS
#define GRAPHICS_ADAPTERS

#include <stddef.h>

#include "pix.h"

void vga_interleave(uint8_t *restrict dst, const uint8_t *restrict src,
const struct raster_desc *desc, size_t lines, size_t scanline);

struct pix_rgba8 ega_palette(size_t idx);

struct pix_rgba8 cga_palette(size_t idx);

#endif /* GRAPHICS_ADAPTERS */
