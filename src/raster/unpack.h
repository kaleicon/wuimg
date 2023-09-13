// SPDX-License-Identifier: 0BSD
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

void unpack_strip(void *restrict dst, const void *restrict src,
size_t n, uint8_t bitdepth, enum pix_attr attr, enum unpack_op op);

void unpack_or_copy_strip(void *restrict dst, const void *restrict src,
size_t n, uint8_t bitdepth, enum pix_attr attr, enum unpack_op op);

uint8_t unpack_depth(uint8_t bitdepth, enum pix_attr attr, enum unpack_op op);

size_t unpack_stride(size_t n, uint8_t bitdepth, enum pix_attr attr,
enum unpack_op op);

#endif // COMMON_UNPACK
