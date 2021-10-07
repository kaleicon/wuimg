#ifndef RASTER_PIX
#define RASTER_PIX

#include <stdint.h>

enum pix_layout {
	// target:   red  |  green |  blue  | alpha/one
	pix_gray =                            3,
	pix_rgba =          1 << 4 | 2 << 2 | 3,
	pix_argb = 1 << 6 | 2 << 4 | 3 << 2,
	pix_bgra = 2 << 6 | 1 << 4          | 3,
	pix_abgr = 3 << 6 | 2 << 4 | 1 << 2,
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

void pix_swizzle_mask(uint8_t swizzle[static 4], enum pix_layout layout);

void pix_expand555(void *restrict out, uint16_t word);

void pix_rgb8_to_rgba8(struct pix_rgba8 *dst, const struct pix_rgb8 *src,
size_t nmemb);

#endif /* RASTER_PIX */
