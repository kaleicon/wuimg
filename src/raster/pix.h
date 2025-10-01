// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef RASTER_PIX
#define RASTER_PIX

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum pix_color {
	pix_red = 0,
	pix_green = 1,
	pix_blue = 2,
	pix_alpha = 3,
	pix_color_total,
};

/* Specifies on which channel is each color located. r = 3 means red is located
 * in the fourth channel. */
#define PIX_LAYOUT_PACK(r, g, b, a) ( (r) << (pix_red * 2) \
	| (g) << (pix_green * 2) \
	| (b) << (pix_blue * 2) \
	| (a) << (pix_alpha * 2) )
enum pix_layout {
	pix_gray = PIX_LAYOUT_PACK(0, 0, 0, 1),
	pix_rgba = PIX_LAYOUT_PACK(0, 1, 2, 3),
	pix_grba = PIX_LAYOUT_PACK(1, 0, 2, 3),
	pix_argb = PIX_LAYOUT_PACK(1, 2, 3, 0),
	pix_gbra = PIX_LAYOUT_PACK(2, 0, 1, 3),
	pix_bgra = PIX_LAYOUT_PACK(2, 1, 0, 3),
	pix_abgr = PIX_LAYOUT_PACK(3, 2, 1, 0),
};

enum pix_attr {
	pix_normal = 0,
	pix_signed,
	pix_inverted,
	pix_float,
};

struct pix_rgb8 {
	uint8_t r, g, b;
};

struct pix_rgba8 {
	uint8_t r, g, b, a;
};

const char * pix_attr_str(enum pix_attr attr);

enum pix_layout pix_layout_pack(uint8_t l1, uint8_t l2, uint8_t l3, uint8_t l4);

uint8_t pix_layout_offset(enum pix_layout layout, enum pix_color color);

void pix_layout_swizzle(void *restrict dst, const void *restrict src,
size_t size, uint8_t nmemb, enum pix_layout dst_l, enum pix_layout src_l);

void pix_layout_swizzle_buf(void *buf, uint8_t size, uint8_t nmemb,
enum pix_layout dst_l, enum pix_layout src_l);

enum pix_layout pix_layout_mul(enum pix_layout l1, enum pix_layout l2);

uint8_t pix_layout_min_map(uint8_t map[static 4], enum pix_layout layout);

uint8_t pix_layout_repr(uint8_t str[static 4], enum pix_layout layout);

void pix_layout_print(enum pix_layout layout, FILE *out);

#endif /* RASTER_PIX */
