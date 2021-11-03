#include <stddef.h>
#include <limits.h>

#include "pix.h"

uint8_t pix_layout_offset(const enum pix_layout layout,
const enum pix_color color) {
	return (layout >> (color*2)) & 0x03;
}

void pix_swizzle_mask(uint8_t swizzle[static 4], const enum pix_layout layout) {
	for (enum pix_color i = 0; i < pix_color_total; ++i) {
		swizzle[i] = pix_layout_offset(layout, i);
	}
}

void pix_rgb8_to_rgba8(struct pix_rgba8 *dst, const struct pix_rgb8 *src,
const size_t nmemb) {
	for (size_t n = 0; n < nmemb; ++n) {
		dst[n].r = src[n].r;
		dst[n].g = src[n].g;
		dst[n].b = src[n].b;
		dst[n].a = 0xff;
	}
}
