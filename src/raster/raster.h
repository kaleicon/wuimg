#ifndef RASTER_LIB
#define RASTER_LIB

#include <stdbool.h>

#include "pal.h"
#include "pix.h"

struct raster_desc {
	size_t w, h;
	uint8_t ch, bitdepth;
	enum pix_layout layout:8;
	enum pix_attr attr:8;
	bool mirror;
};

size_t raster_size(const struct raster_desc *desc);

const char * raster_geom_verify(uint8_t ch, uint8_t bitdepth,
enum pix_attr attr, bool paletted);

bool raster_test_overflow(size_t w, size_t h, uint8_t ch, uint8_t bitdepth,
align_t alignment);

bool raster_normalize(struct raster_desc *desc);

#endif /* RASTER_LIB */
