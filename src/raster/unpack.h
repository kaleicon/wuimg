#ifndef COMMON_UNPACK
#define COMMON_UNPACK

#include <stdint.h>

#include "../common.h"
#include "pix.h"

enum unpack_op {
	op_noop = 0,
	op_unpack,
	op_expand,
	op_pack,
};


void unpack_strip(void *restrict out, const void *restrict src,
size_t width, size_t height, size_t alignment, enum pix_attr attr,
enum unpack_op op, size_t bitdepth);

uint8_t unpack_depth(enum pix_attr attr, enum unpack_op op, size_t bitdepth);

size_t unpack_stride(size_t width, enum pix_attr attr, enum unpack_op op,
size_t bitdepth);


void strip_interleave(void *restrict out, const void *restrict src,
size_t dims, size_t planes, size_t bitdepth);

void strip_spread(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t ch);

void strip_swizzle(uint8_t *dst, const uint8_t *src, size_t w, size_t h,
size_t ch, size_t bitdepth, size_t alignment, enum pix_layout src_layout,
enum pix_layout dst_layout);

/*void strip_swizzle(uint8_t *dst, const uint8_t *src,
const struct raster_desc *restrict desc, enum pix_layout target_layout);*/

#endif // COMMON_UNPACK
