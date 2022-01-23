#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>

#include "../common.h"
#include "pix.h"

uint8_t pix_layout_offset(const enum pix_layout layout,
const enum pix_color color) {
	return (layout >> (color*2)) & 0x03;
}

void pix_layout_print(const enum pix_layout layout) {
	for (enum pix_color i = 0; i < pix_color_total; ++i) {
		putchar(pix_layout_offset(layout, i) + '0');
	}
}

void pix_swizzle_mask(uint8_t swizzle[static 4], const enum pix_layout layout) {
	for (enum pix_color i = 0; i < pix_color_total; ++i) {
		swizzle[i] = pix_layout_offset(layout, i);
	}
}

static inline void pix_set_common(unsigned char *restrict dst,
const unsigned char *restrict src, const size_t size, const size_t nmemb) {
	for (size_t i = 0; i < nmemb; ++i) {
		memcpy(dst, src, size);
		dst += size;
	}
}

static void pix_set4(unsigned char *restrict dst,
const unsigned char *restrict src, const size_t nmemb) {
	pix_set_common(dst, src, 4, nmemb);
}

static void pix_set3(unsigned char *restrict dst,
const unsigned char *restrict src, const size_t nmemb) {
	pix_set_common(dst, src, 4, nmemb);
}

static void pix_set2(unsigned char *restrict dst,
const unsigned char *restrict src, const size_t nmemb) {
	pix_set_common(dst, src, 4, nmemb);
}

void pix_set(void *restrict dst, const void *restrict pix,
const size_t pix_size, const size_t nmemb) {
	unsigned char *restrict d = dst;
	const unsigned char *restrict s = pix;

	if (!memchk(s + 1, s[0], pix_size - 1)) {
		memset(d, s[0], nmemb * pix_size);
	} else {
		switch (pix_size) {
		case 2: pix_set2(d, s, nmemb); break;
		case 3: pix_set3(d, s, nmemb); break;
		case 4: pix_set4(d, s, nmemb); break;
		default: pix_set_common(d, s, pix_size, nmemb);
		}
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
