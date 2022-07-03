#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>

#include "common.h"
#include "raster/pix.h"

const char * pix_attr_str(const enum pix_attr attr) {
	switch (attr) {
	case pix_normal: return "normal";
	case pix_signed: return "signed";
	case pix_inverted: return "inverted";
	case pix_float: return "float";
	case pix_packing_332: return "332";
	case pix_packing_1555: return "1555";
	}
	return "???";
}

uint8_t pix_layout_offset(const enum pix_layout layout,
const enum pix_color color) {
	return (layout >> (color*2)) & 0x03;
}

void pix_layout_swizzle(void *buf, const size_t nmemb, const size_t size,
const enum pix_layout layout) {
	uint8_t tmp[8*4];
	if (nmemb * size < sizeof(tmp)) {
		for (uint8_t z = 0; z < nmemb; ++z) {
			const size_t src_z = pix_layout_offset(layout, z);
			memcpy(tmp + z*size, (uint8_t *)buf + src_z*size, size);
		}
		memcpy(buf, tmp, size * nmemb);
	}
}

static inline void pix_set_common(uint8_t *restrict dst,
const void *restrict src, const size_t nmemb, const size_t size) {
	for (size_t i = 0; i < nmemb; ++i) {
		memcpy(dst + i*size, src, size);
	}
}

static void pix_set4(void *restrict dst, const void *restrict src,
const size_t nmemb) {
	pix_set_common(dst, src, nmemb, 4);
}

static void pix_set3(uint8_t *restrict dst, const void *restrict src,
const size_t nmemb) {
	const size_t size = 3;
	uint32_t triple;
	memcpy(&triple, src, size);
	size_t i = 0;
	while (i < nmemb - 1) {
		memcpy(dst + i*size, &triple, sizeof(triple));
		++i;
	}
	memcpy(dst + i*size, &triple, size);
}

static void pix_set2(void *restrict dst, const void *restrict src,
const size_t nmemb) {
	pix_set_common(dst, src, nmemb, 2);
}

void pix_set(void *restrict dst, const void *restrict pix,
const size_t pix_size, const size_t nmemb) {
	switch (pix_size) {
	case 1: memset(dst, *((uint8_t *)pix), nmemb); break;
	case 2: pix_set2(dst, pix, nmemb); break;
	case 3: pix_set3(dst, pix, nmemb); break;
	case 4: pix_set4(dst, pix, nmemb); break;
	default:
		pix_set_common(dst, pix, nmemb, pix_size);
		break;
	}
}
