#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../wudefs.h"
#include "../common.h"
#include "composite.h"

static void blend_rgba_on_rgba_pixel(unsigned char *restrict d,
const unsigned char *restrict s) {
	const unsigned int ch = 4;
	switch (s[3]) {
	case 0xff: // Only case where (1 - src.A / 255) == 0
		memcpy(d, s, ch);
		return;
	case 0x00: // With these and the above, only case where blend.A == 0
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
	case 0xff: // Only case where (1 - src.A / 255) == 0
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
	/* Premultiplied alpha blending, as given by the WebP docs:

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
const void *restrict src, const struct frame *fr) {
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
const void *restrict src, const struct frame *fr) {
	size_t dst_pos = (fr->y * w + fr->x) * ch;
	size_t src_pos = 0;
	for (size_t i = 0; i < fr->h; ++i) {
		memcpy((uint8_t *)dst + dst_pos, (uint8_t *)src + src_pos,
			fr->w * ch);
		dst_pos += w * ch;
		src_pos += fr->w * ch;
	}
}

void composite_frame_alpha_blend(struct raw_img *img,
const unsigned char *restrict src, const struct frame_info *frame) {
	size_t dst_pos = (frame->y * img->w + frame->x) * img->channels;
	size_t src_pos = 0;
	for (size_t i = 0; i < frame->h; ++i) {
		blend_row(img->data + dst_pos, src + src_pos, frame->w,
			img->channels);
		dst_pos += img->w * img->channels;
		src_pos += frame->w * 4;
	}
}

void composite_frame_overwrite(struct raw_img *img,
const unsigned char *restrict src, const struct frame_info *frame) {
	const size_t ch = img->channels;

	const size_t src_width = frame->w * ch;
	size_t dst_pos = (frame->y * img->w + frame->x) * ch;
	size_t src_pos = 0;
	for (size_t i = 0; i < frame->h; ++i) {
		memcpy(img->data + dst_pos, src + src_pos, src_width);
		dst_pos += img->w * ch;
		src_pos += src_width;
	}
}


void composite_clear(struct raw_img *img, const struct frame_info *frame,
const int c) {
	const size_t ch = img->channels;
	unsigned char *pos = img->data
		+ ((frame->y * img->w + frame->x) * ch);

	for (size_t i = 0; i < frame->h; ++i) {
		memset(pos, c, frame->w * ch);
		pos += img->w * ch;
	}
}
