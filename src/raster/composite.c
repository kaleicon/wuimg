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
	/* The formula for premultiplied alpha blending, as given by the WebP
	 * docs:

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

void composite_frame_alpha_blend(struct raw_img *img,
const unsigned char *restrict src, const struct anim_frame *frame) {
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
const unsigned char *restrict src, const struct anim_frame *frame) {
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

static inline void color_set_common(unsigned char *restrict dst,
const unsigned char *restrict src, const size_t size, const size_t nmemb) {
	for (size_t i = 0; i < nmemb; ++i) {
		memcpy(dst, src, size);
		dst += size;
	}
}

static void color_set4(unsigned char *restrict dst,
const unsigned char *restrict src, const size_t nmemb) {
	color_set_common(dst, src, 4, nmemb);
}

static void color_set3(unsigned char *restrict dst,
const unsigned char *restrict src, const size_t nmemb) {
	color_set_common(dst, src, 3, nmemb);
}

static void color_set2(unsigned char *restrict dst,
const unsigned char *restrict src, size_t nmemb) {
	color_set_common(dst, src, 2, nmemb);
}

void color_set(void *restrict dst, const void *restrict src,
const size_t size, const size_t nmemb) {
	unsigned char *restrict d = dst;
	const unsigned char *restrict s = src;

	if (!memchk(s + 1, s[0], size - 1)) {
		memset(d, s[0], nmemb * size);
	} else {
		switch (size) {
		case 2: color_set2(d, s, nmemb); break;
		case 3: color_set3(d, s, nmemb); break;
		case 4: color_set4(d, s, nmemb); break;
		default: color_set_common(d, s, size, nmemb);
		}
	}
}

void composite_clear(struct raw_img *img, const struct anim_frame *frame,
const int c) {
	const size_t ch = img->channels;
	unsigned char *pos = img->data
		+ ((frame->y * img->w + frame->x) * ch);

	for (size_t i = 0; i < frame->h; ++i) {
		memset(pos, c, frame->w * ch);
		pos += img->w * ch;
	}
}

void copy_unaffected(struct raw_img *img, const unsigned char *restrict prev,
const struct anim_frame *frame) {
	const size_t ch = img->channels;

	size_t offset = (frame->y * img->w + frame->x) * ch;
	const size_t copy_stride = (img->w - frame->w) * ch;
	const size_t tail = (img->w * img->h - (frame->h - 1) * img->w
		- frame->w) * ch - offset;

	memcpy(img->data, prev, offset);
	offset += frame->w * ch;
	for (size_t i = 0; i < frame->h - 1; ++i) {
		memcpy(img->data + offset, prev + offset, copy_stride);
		offset += img->w * ch;
	}
	memcpy(img->data + offset, prev + offset, tail);
}
