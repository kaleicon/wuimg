#ifndef COMMON_UNPACK
#define COMMON_UNPACK

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "../common.h"
#include "raster.h"

enum unpack_op {
	op_noop = 0,
	op_unpack,
	op_expand,
	op_expand_invert,
	op_pack,
	op_pack_float,
};


void strip_unpack(void *restrict out, const void *restrict src, size_t width,
size_t height, size_t boundary, enum unpack_op op, size_t bitdepth);

void strip_expand332(uint8_t *restrict out, const uint8_t *restrict src,
size_t width, size_t height, size_t boundary);

void strip_interleave(void *restrict out, const void *restrict src,
size_t dims, size_t planes, size_t bitdepth);

void strip_spread(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t ch);

void strip_swizzle(uint8_t *dst, const uint8_t *src,
const struct raster_desc *restrict desc, enum pix_layout target_layout);

#endif // COMMON_UNPACK
