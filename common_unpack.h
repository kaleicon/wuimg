#ifndef COMMON_UNPACK
#define COMMON_UNPACK

#include <stdlib.h>
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


void strip_invert8(u_int8_t *out, size_t len);

u_int8_t * strip_expand332(u_int8_t *restrict out,
const u_int8_t *restrict src, size_t width, size_t height,
unsigned char boundary);

void * strip_unpack(void *restrict out, const void *restrict src, size_t width,
size_t height, size_t boundary, enum unpack_op op, size_t bitdepth);


void * strip_colormap(void *restrict out, const u_int8_t *restrict src,
const void *cm, size_t width, size_t height, u_int8_t boundary,
u_int8_t channels, u_int8_t bitdepth);

#endif // COMMON_UNPACK
