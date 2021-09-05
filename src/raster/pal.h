#ifndef RASTER_PAL
#define RASTER_PAL

#include "pix.h"

struct raster_pal {
	struct pix_rgba8 color[256];
};

void raster_pal_expand(void *restrict dst, const uint8_t *restrict src,
const struct raster_pal *cm, size_t width, size_t height, uint8_t alignment,
uint8_t channels);

#endif /* RASTER_PAL */
