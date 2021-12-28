#ifndef RASTER_PIX
#define RASTER_PIX

#include <stdint.h>

enum pix_color {
	// Color order for pix_layout.
	pix_red,
	pix_green,
	pix_blue,
	pix_alpha,
	pix_color_total,
};

enum pix_layout {
	// color:   red  |  green |  blue  | alpha/one
	pix_gray =                           1 << 6,
	pix_rgba =         1 << 2 | 2 << 4 | 3 << 6,
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

void pix_rgb8_to_rgba8(struct pix_rgba8 *dst, const struct pix_rgb8 *src,
size_t nmemb);

#endif /* RASTER_PIX */
