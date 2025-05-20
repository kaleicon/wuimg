// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#ifndef COMMON_UNPACK
#define COMMON_UNPACK

#include <stdbool.h>
#include <stdint.h>

#include "pix.h"

#include "bitfield.h"

enum unpack_op {
	op_noop = 0,
	op_unpack,
	op_pack,
	op_bitfield,
};

void unpack_strip(void *restrict dst, const void *restrict src,
size_t n, uint8_t bitdepth, enum pix_attr attr, enum unpack_op op,
const void *arg);

size_t unpack_stride(size_t n, uint8_t bitdepth, enum pix_attr attr,
enum unpack_op op, const void *arg);


struct remap_info {
	bool scale;
	bool exact;
	uint8_t depth_sh;
	enum pix_attr attr:8;
	uint32_t var;
	uint64_t mul;
};

void remap_scale(void *dst, const void *src, size_t width,
struct remap_info info);

struct remap_info remap_scale_info(uint32_t maxval, uint8_t outdepth,
enum pix_attr attr);

#endif // COMMON_UNPACK
