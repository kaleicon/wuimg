#ifndef RASTER_PIX
#define RASTER_PIX

#include <stdint.h>

enum pix_color {
	// Color order for pix_layout.
	pix_red = 0,
	pix_green = 1,
	pix_blue = 2,
	pix_alpha = 3,
	pix_color_total,
};

enum pix_layout {
	// color:   red  |  green |  blue  | alpha/one
	pix_gray =                           1 << 6,
	pix_rgba =         1 << 2 | 2 << 4 | 3 << 6,
	pix_rbga =         2 << 2 | 1 << 4 | 3 << 6, // Meant for testing
	pix_argb =     1 | 2 << 2 | 3 << 4,
	pix_bgra =     2 | 1 << 2          | 3 << 6,
	pix_abgr =     3 | 2 << 2 | 1 << 4,
};

enum pix_subsampling {
	// axis:       horz | vert
	pix_yuv444 =          0,
	pix_yuv420 = 1 << 2 | 1,
};

enum pix_attr {
	pix_normal,
	pix_inverted,
	pix_float,
	pix_packing_332,
	pix_packing_1555,
};

struct pix_rgb8 {
	uint8_t r, g, b;
};

struct pix_rgba8 {
	uint8_t r, g, b, a;
};

uint8_t pix_layout_offset(enum pix_layout layout, enum pix_color color);

void pix_layout_print(const enum pix_layout layout);

void pix_swizzle_mask(uint8_t swizzle[static 4], enum pix_layout layout);

void pix_set_word(uint16_t *restrict dst, const uint16_t word,
const size_t nmemb);

void pix_set(void *restrict dst, const void *restrict pix,
const size_t pix_size, const size_t nmemb);

void pix_rgb8_to_rgba8(struct pix_rgba8 *dst, const struct pix_rgb8 *src,
size_t nmemb);

#endif /* RASTER_PIX */
