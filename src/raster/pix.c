#include <stddef.h>
#include <limits.h>

#include "pix.h"

void pix_swizzle_mask(uint8_t swizzle[static 4], const enum pix_layout layout) {
	const uint8_t mask = 0x03;
	for (size_t i = 0; i < 4; ++i) {
		swizzle[i] = (layout >> (6 - i*2)) & mask;
	}
}

void pix_expand555(void *restrict out, uint16_t word) {
	uint8_t *o = out;
	const uint8_t range = (1 << 5) - 1;
	const uint8_t p = 6;
	const uint16_t scale = (UCHAR_MAX << p) / range + 1;

	for (uint8_t n = 0; n < 3; ++n) {
		const uint8_t m = n*5;
		o[n] = (uint8_t)( (scale * (word & (range << m))) >> (p+m) );
	}
}

void pix_rgb8_to_rgba8(struct pix_rgba8 *dst, const struct pix_rgb8 *src,
const size_t n) {
	for (size_t m = 0; m < n; ++m) {
		dst[m].r = src[m].r;
		dst[m].g = src[m].g;
		dst[m].b = src[m].b;
		dst[m].a = 0xff;
	}
}
