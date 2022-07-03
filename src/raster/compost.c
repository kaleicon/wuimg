#include <string.h>

#include "compost.h"

static void blend_rgba_on_rgba_pixel(unsigned char *restrict d,
const unsigned char *restrict s) {
	const unsigned int ch = 4;
	switch (s[3]) {
	case 0xff: // (1 - src.A / 255) == 0
		memcpy(d, s, ch);
		return;
	case 0x00: // blend.A == 0
		return;
	}

	const int blend_a = s[3] + d[3];
	for (unsigned int k = 0; k < ch - 1; ++k) {
		d[k] = (unsigned char)(
			(s[k] * s[3] + d[k] * d[3]) / blend_a
		);
	}
	d[3] = (unsigned char)blend_a;
}

static void blend_rgba_on_rgb_pixel(unsigned char *restrict d,
const unsigned char *restrict s) {
	const unsigned int ch = 3;
	switch (s[3]) {
	case 0xff: // (1 - src.A / 255) == 0
		memcpy(d, s, ch);
		return;
	case 0x00:
		return;
	}

	const int dst_a = 0xff;
	const int blend_a = s[3] + dst_a;
	for (unsigned int k = 0; k < ch; ++k) {
		d[k] = (unsigned char)(
			(s[k] * s[3] + d[k] * dst_a) / blend_a
		);
	}
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

	const size_t src_ch = 4;
	switch (ch) {
	case 3:
		for (size_t j = 0; j < len; ++j) {
			blend_rgba_on_rgb_pixel(dst + j*ch, src + j*src_ch);
		}
		break;
	case 4:
		for (size_t j = 0; j < len; ++j) {
			blend_rgba_on_rgba_pixel(dst + j*ch, src + j*src_ch);
		}
	}
}

void compost_alpha_blend(void *restrict dst, const size_t w, const uint8_t ch,
const void *restrict src, const struct frame_info *fr) {
	size_t dst_pos = (fr->y * w + fr->x) * ch;
	size_t src_pos = 0;
	for (size_t i = 0; i < fr->h; ++i) {
		blend_row((uint8_t *)dst + dst_pos, (uint8_t *)src + src_pos,
			fr->w, ch);
		dst_pos += w * ch;
		src_pos += fr->w * 4;
	}
}

void compost_overwrite(void *restrict dst, const size_t w, const uint8_t ch,
const void *restrict src, const struct frame_info *fr) {
	size_t dst_pos = (fr->y * w + fr->x) * ch;
	size_t src_pos = 0;
	for (size_t i = 0; i < fr->h; ++i) {
		memcpy((uint8_t *)dst + dst_pos, (uint8_t *)src + src_pos,
			fr->w * ch);
		dst_pos += w * ch;
		src_pos += fr->w * ch;
	}
}

void compost_clear(void *restrict dst, const size_t w, const uint8_t ch,
const int c, const struct frame_info *fr) {
	size_t dst_pos = (fr->y * w + fr->x) * ch;
	for (size_t i = 0; i < fr->h; ++i) {
		memset((uint8_t *)dst + dst_pos, c, fr->w * ch);
		dst_pos += w * ch;
	}
}

bool compost_bounds_check(const size_t w, const size_t h,
const struct frame_info *fr) {
	return (fr->x + fr->w <= w) && (fr->y + fr->h <= h);
}
