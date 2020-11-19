#ifndef COMMON_UNPACK
#define COMMON_UNPACK

#include <stdio.h>
#include <stdbool.h>

enum unpack_op {
	noop = 0,
	unpack,
	expand,
	expand_invert,
	pack,
	pack_float,
};

struct colormap {
	unsigned char r, g, b, a;
};


void strip_invert8(uint8_t *out, size_t len);

uint8_t * strip_expand332(uint8_t *restrict out,
const uint8_t *restrict src, size_t width, size_t height,
unsigned char boundary);

void * strip_unpack(void *restrict out, const void *restrict src, size_t width,
size_t height, size_t boundary, enum unpack_op op, size_t bitdepth);


void strip_colormap(void *restrict out, const uint8_t *restrict src,
const void *cm, size_t width, size_t height, uint8_t boundary,
uint8_t channels, uint8_t bitdepth);

void * strip_map_unpack(FILE *ifp, const size_t width, const size_t height,
const size_t boundary, const enum unpack_op op, const size_t bitdepth);

#endif // COMMON_UNPACK
