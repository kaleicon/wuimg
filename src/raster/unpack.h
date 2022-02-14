#ifndef COMMON_UNPACK
#define COMMON_UNPACK

#include <stdint.h>

#include "pix.h"

enum unpack_op {
	op_noop = 0,
	op_unpack,
	op_expand,
	op_pack,
};

void unpack_strip(void *restrict out, const void *restrict src,
size_t n, uint8_t bitdepth, enum pix_attr attr, enum unpack_op op);

void unpack_or_copy_strip(void *restrict out, const void *restrict src,
size_t n, uint8_t bitdepth, enum pix_attr attr, enum unpack_op op);

uint8_t unpack_depth(uint8_t bitdepth, enum pix_attr attr, enum unpack_op op);

size_t unpack_stride(size_t n, uint8_t bitdepth, enum pix_attr attr,
enum unpack_op op);


void strip_spread(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t ch);

void strip_swizzle(void *dst, size_t w, uint8_t ch, size_t elem_size,
enum pix_layout dst_layout, enum pix_layout src_layout);

#endif // COMMON_UNPACK
