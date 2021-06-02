#ifndef COMMON_UNPACK
#define COMMON_UNPACK

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../common.h"

enum unpack_op {
	op_noop = 0,
	op_unpack,
	op_expand,
	op_expand_invert,
	op_pack,
	op_pack_float,
};

struct colormap {
	unsigned char r, g, b, a;
};


void strip_unpack(void *restrict out, const void *restrict src, size_t width,
size_t height, size_t boundary, enum unpack_op op, size_t bitdepth);

void strip_colormap(void *restrict out, const uint8_t *restrict src,
const void *cm, size_t width, size_t height, size_t boundary, size_t channels,
size_t bitdepth);

void strip_expand332(uint8_t *restrict out, const uint8_t *restrict src,
size_t width, size_t height, size_t boundary);


void strip_interleave(void *restrict out, const void *restrict src,
size_t dims, size_t planes, size_t bitdepth);

void * strip_map_colormap(FILE *ifp, const struct colormap *cm, size_t width,
size_t height, size_t boundary, size_t channels, size_t bitdepth);

void * strip_map_unpack(FILE *ifp, size_t width, size_t height,
size_t boundary, enum unpack_op op, size_t bitdepth);


void pixel_expand555(void *restrict out, uint16_t word);

#endif // COMMON_UNPACK
