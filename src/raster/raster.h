#ifndef RASTER_LIB
#define RASTER_LIB

#include "pix.h"
#include "pal.h"

struct raster_desc {
	struct raster_pal *palette;
	size_t w, h;
	uint8_t ch, bitdepth, alignment;
	enum pix_layout layout:8;
	enum pix_attr attr:8;
};

const char * raster_geom_verify(const struct raster_pal *palette,
const uint8_t ch, const uint8_t bitdepth, const enum pix_attr attr);

void raster_free(struct raster_desc *desc);

size_t raster_stride(const struct raster_desc *desc);

size_t raster_size(const struct raster_desc *desc);

void raster_normalize(struct raster_desc *desc);

#endif /* RASTER_LIB */
