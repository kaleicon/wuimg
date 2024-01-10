// SPDX-License-Identifier: 0BSD
#include <string.h>

#include "raster/pix.h"

const char * pix_attr_str(const enum pix_attr attr) {
	switch (attr) {
	case pix_normal: return "normal";
	case pix_signed: return "signed";
	case pix_inverted: return "inverted";
	case pix_float: return "float";
	}
	return "???";
}

enum pix_layout pix_layout_pack(uint8_t l1, uint8_t l2, uint8_t l3, uint8_t l4) {
	return (enum pix_layout)PIX_LAYOUT_PACK(l1, l2, l3 ,l4);
}

uint8_t pix_layout_offset(const enum pix_layout layout,
const enum pix_color color) {
	return (layout >> (color*2)) & 0x03;
}

void pix_layout_swizzle(void *buf, const size_t size, const size_t nmemb,
const enum pix_layout layout) {
	uint8_t tmp[16*4];
	if (nmemb * size < sizeof(tmp)) {
		for (uint8_t z = 0; z < nmemb; ++z) {
			const size_t src_z = pix_layout_offset(layout, z);
			memcpy(tmp + z*size, (uint8_t *)buf + src_z*size, size);
		}
		memcpy(buf, tmp, size * nmemb);
	}
}

enum pix_layout pix_layout_mul(const enum pix_layout l1, const enum pix_layout l2) {
	uint8_t u[4] = {0, 1, 2, 3};
	pix_layout_swizzle(u, 1, 4, l1);
	pix_layout_swizzle(u, 1, 4, l2);
	return pix_layout_pack(u[0], u[1], u[2], u[3]);
}

void pix_layout_print(const enum pix_layout layout, FILE *out) {
	uint8_t rgba[] = {'r', 'g', 'b', 'a', '\n'};
	pix_layout_swizzle(rgba, 1, sizeof(rgba) - 1, layout);
	fwrite(rgba, 1, sizeof(rgba), out);
}

uint8_t pix_layout_invert(uint8_t map[static 4], const enum pix_layout layout) {
	const uint8_t len = pix_color_total;
	uint8_t seen[4] = {0};
	uint8_t pos = 0;
	for (uint8_t color = 0; color < len; ++color) {
		const uint8_t ch = pix_layout_offset(layout, color);
		if (!(seen[ch] & 1)) {
			seen[ch] |= 1;
			seen[color] |= 2;
			map[pos] = color;
			++pos;
		}
	}
	uint8_t i = pos;
	for (uint8_t ch = 0; ch < len; ++ch) {
		if (!(seen[ch] & 2)) {
			map[i] = ch;
			++i;
		}
	}
	return pos;
}
