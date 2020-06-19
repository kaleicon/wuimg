#ifndef COMMON_UNPACK
#define COMMON_UNPACK

#include <stddef.h>

enum unpack_op {
	noop = 0,
	unpack,
	expand,
	expand_invert,
};

struct colormap {
	unsigned char r, g, b, a;
};

typedef unsigned char * (*unpack_func_t)(unsigned char *restrict data,
	unsigned char *restrict buf, const size_t width, const size_t height,
	const unsigned char boundary);

typedef unsigned char * (*colormap_func_t)(unsigned char *restrict data,
	unsigned char *restrict buf, const struct colormap *cm,
	const size_t width, const size_t height, const unsigned char boundary);

size_t scanline_length(const size_t width, const unsigned char bitdepth,
const unsigned char boundary);


void strip_invert8(unsigned char *data, const size_t len);

unsigned char * strip_invert4(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary);

unsigned char * strip_invert2(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary);

unsigned char * strip_invert1(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary);

unsigned char * strip_expand4(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary);

unsigned char * strip_expand2(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary);

unsigned char * strip_expand1(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary);

unsigned char * strip_unpack4(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary);

unsigned char * strip_unpack2(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary);

unsigned char * strip_unpack1(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary);

unsigned char * strip_unpack(unsigned char *restrict data,
unsigned char *restrict buf, const size_t width, const size_t height,
const unsigned char boundary, const unsigned char bitdepth,
const enum unpack_op op);


unsigned char * strip_colormap_rgb8(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary);

unsigned char * strip_colormap_rgb4(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary);

unsigned char * strip_colormap_rgb2(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary);

unsigned char * strip_colormap_rgb1(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary);

unsigned char * strip_colormap_rgba8(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary);

unsigned char * strip_colormap_rgba4(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary);

unsigned char * strip_colormap_rgba2(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary);

unsigned char * strip_colormap_rgba1(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary);

unsigned char * strip_colormap(unsigned char *restrict data,
unsigned char *restrict buf, const void *cm, const size_t width,
const size_t height, const unsigned char boundary, const unsigned char bitdepth,
const unsigned char channels);

#endif // COMMON_UNPACK
