// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdbool.h>
#include <string.h>

#include "misc/common.h"
#include "raster/pix.h"

const char * pix_attr_str(const enum pix_attr attr) {
	switch (attr) {
	case pix_normal: return "normal";
	case pix_signed: return "signed";
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

void pix_layout_swizzle(void *restrict dst, const void *restrict src,
const size_t size, const uint8_t nmemb, const enum pix_layout dst_l,
const enum pix_layout src_l) {
	for (uint8_t z = nmemb; z; --z) {
		const size_t src_z = pix_layout_offset(src_l, z-1);
		const size_t dst_z = pix_layout_offset(dst_l, z-1);
		memcpy((uint8_t *)dst + dst_z*size,
			(uint8_t *)src + src_z*size, size);
	}
}

void pix_layout_swizzle_buf(void *buf, const uint8_t size, const uint8_t nmemb,
const enum pix_layout dst_l, const enum pix_layout src_l) {
	uint8_t tmp[16*4];
	if (nmemb * size < sizeof(tmp)) {
		memcpy(tmp, buf, size * nmemb);
		pix_layout_swizzle(tmp, buf, size, nmemb, dst_l, src_l);
		memcpy(buf, tmp, size * nmemb);
	}
}

enum pix_layout pix_layout_mul(const enum pix_layout l1, const enum pix_layout l2) {
	uint8_t u[4] = {0, 1, 2, 3};
	pix_layout_swizzle_buf(u, sizeof(*u), ARRAY_LEN(u), pix_rgba, l1);
	pix_layout_swizzle_buf(u, sizeof(*u), ARRAY_LEN(u), pix_rgba, l2);
	return pix_layout_pack(u[0], u[1], u[2], u[3]);
}

uint8_t pix_layout_min_map(uint8_t map[static 4], const enum pix_layout layout) {
	bool seen[4] = {0};
	uint8_t ch = 0;
	for (uint8_t z = 0; z < pix_color_total; ++z) {
		uint8_t dst_z = pix_layout_offset(layout, z);
		if (!seen[dst_z]) {
			map[ch] = dst_z;
			ch += 1;
			seen[dst_z] = true;
		}
	}
	for (uint8_t z = ch; z < pix_color_total; ++z) {
		map[z] = 0;
	}
	return ch;
}

uint8_t pix_layout_repr(uint8_t str[static 4], const enum pix_layout layout) {
	uint8_t map[4];
	const uint8_t ch = pix_layout_min_map(map, layout);
	const uint8_t rgba[] = {'r', 'g', 'b', 'a'};
	for (uint8_t z = 0; z < ch; ++z) {
		str[map[z]] = rgba[z];
	}
	return ch;
}

void pix_layout_print(const enum pix_layout layout, FILE *out) {
	uint8_t str[4];
	fwrite(str, 1, pix_layout_repr(str, layout), out);
	fputc('\n', out);
}
