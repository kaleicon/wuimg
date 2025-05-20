// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <string.h>

#include "compost.h"

static void blend_rgba_pixel(unsigned char *restrict d,
const unsigned char *restrict s) {
	const unsigned int ch = 4;
	switch (s[3]) {
	case 0xff: // (1 - src.A / 255) == 0
		memcpy(d, s, ch);
		return;
	case 0x00: // blend.A == 0
		return;
	}

	// FIXME: Proper alpha blending is done on linear light
	const int blend_a = s[3] + d[3];
	for (unsigned int k = 0; k < ch - 1; ++k) {
		d[k] = (unsigned char)(
			(s[k] * s[3] + d[k] * d[3]) / blend_a
		);
	}
	d[3] = (unsigned char)blend_a;
}

static void blend_row(unsigned char *restrict dst,
const unsigned char *restrict src, const size_t len, const size_t ch) {
	/* Unassociated alpha blending, as given by the WebP docs:

		blend.A = src.A + dst.A * (1 - src.A / 255)
		if blend.A = 0 then
			blend.RGB = 0
		else
			blend.RGB = (src.RGB * src.A
				+ dst.RGB * dst.A * (1 - src.A / 255)) / blend.A
	*/

	for (size_t j = 0; j < len; ++j) {
		blend_rgba_pixel(dst + j*ch, src + j*ch);
	}
}

void compost_alpha_blend(void *restrict dst, const size_t w,
const void *restrict src, const struct compost *reg) {
	const uint8_t ch = 4;
	size_t dst_pos = (reg->y * w + reg->x) * ch;
	size_t src_pos = 0;
	for (size_t i = 0; i < reg->h; ++i) {
		blend_row((uint8_t *)dst + dst_pos,
			(const uint8_t *)src + src_pos, reg->w, ch);
		dst_pos += w * ch;
		src_pos += reg->w * 4;
	}
}

void compost_overwrite(void *restrict dst, const size_t w, const uint8_t ch,
const void *restrict src, const struct compost *reg) {
	size_t dst_pos = (reg->y * w + reg->x) * ch;
	size_t src_pos = 0;
	for (size_t i = 0; i < reg->h; ++i) {
		memcpy((uint8_t *)dst + dst_pos,
			(const uint8_t *)src + src_pos, reg->w * ch);
		dst_pos += w * ch;
		src_pos += reg->w * ch;
	}
}

void compost_clear(void *restrict dst, const size_t w, const uint8_t ch,
const int c, const struct compost *reg) {
	size_t dst_pos = (reg->y * w + reg->x) * ch;
	for (size_t i = 0; i < reg->h; ++i) {
		memset((uint8_t *)dst + dst_pos, c, reg->w * ch);
		dst_pos += w * ch;
	}
}

bool compost_bounds_check(const size_t w, const size_t h,
const struct compost *reg) {
	return (reg->x + reg->w <= w) && (reg->y + reg->h <= h);
}
