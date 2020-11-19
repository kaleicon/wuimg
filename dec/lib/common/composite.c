#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../../wudefs.h"
#include "../../../common.h"
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

//	if (frame->w == img->w) {
//		blend_row(img->data + dst_pos, src, frame->w * frame->h,
//			img->channels);
//	} else {
		size_t src_pos = 0;
		for (size_t i = 0; i < frame->h; ++i) {
			blend_row(img->data + dst_pos, src + src_pos,
				frame->w, img->channels);
			dst_pos += img->w * img->channels;
			src_pos += frame->w * 4;
		}
//	}
}

void composite_frame_overwrite(struct raw_img *img,
const unsigned char *restrict src, const struct anim_frame *frame) {
	const size_t src_width = frame->w * img->channels;
	size_t dst_pos = (frame->y * img->w + frame->x) * img->channels;

//	if (frame->w == img->w) {
//		memcpy(img->data + dst_pos, src, src_width * frame->h);
//	} else {
		size_t src_pos = 0;
		for (size_t i = 0; i < frame->h; ++i) {
			memcpy(img->data + dst_pos, src + src_pos, src_width);
			dst_pos += img->w * img->channels;
			src_pos += src_width;
		}
//	}
}

/* I wrote this on a whim and turns out it performs better on some cases,
 * but worse on others, so we'll leave it disabled. */
//#define ALIGNED_LOOP
/*
__attribute__((unused))
static unsigned char * color_set4(unsigned char *restrict data,
const void *restrict src, size_t items) {
	const unsigned char *restrict color = src;
	const size_t ch = 4;

#ifdef ALIGNED_LOOP
	if (items >= 3) {
		const size_t size = sizeof(unsigned int);

		for (size_t i = 0; i < size * 2; i += ch) {
			data[0] = color[0];
			data[1] = color[1];
			data[2] = color[2];
			data[3] = color[3];
			data += ch;
		}
		items -= 2;
		size_t tail = (uintptr_t)data % (uintptr_t)size;

		// Get that int
		unsigned int *i_dst = (unsigned int *)
			(data - ((uintptr_t)data % size));
		const unsigned int aligned_color = *(i_dst - 1);

		// Write ints
		do {
			*i_dst = aligned_color;
			++i_dst;
			--items;
		} while (items);
		data = (unsigned char *)i_dst;

		// Finish partial item
		if (tail) {
			do {
				*data = color[tail];
				++data;
				++tail;
			} while (tail < ch);
		}
		return data;
	}
#endif
	for (size_t i = 0; i < items; ++i) {
		data[0] = color[0];
		data[1] = color[1];
		data[2] = color[2];
		data[3] = color[3];
		data += ch;
	}
	return data;
}

__attribute__((unused))
static unsigned char * color_set3(unsigned char *restrict data,
const void *restrict src, size_t items) {
	const unsigned char *restrict color = src;
	const size_t ch = 3;

#ifdef ALIGNED_LOOP
	const size_t size = sizeof(unsigned int);
	if (items >= size * 3 - 1) {

		const size_t align = (uintptr_t)data % size + size;
		for (size_t i = 0; i < align; ++i) {
			memcpy(data, color, ch);
			data += ch;
			--items;
		}

		unsigned int *restrict i_dst = (unsigned int *)data;
		const unsigned int *restrict i_src = i_dst - ch;

		do {
			*i_dst = i_src[0];
			++i_dst;
			*i_dst = i_src[1];
			++i_dst;
			*i_dst = i_src[2];
			++i_dst;

			items -= size;
		} while (items >= size);

		data = (unsigned char *)i_dst;
		memcpy(data, i_src, items * ch);
		return data + (items * ch);
	}
#endif

	for (size_t i = 0; i < items; ++i) {
		data[0] = color[0];
		data[1] = color[1];
		data[2] = color[2];
		data += ch;
	}
	return data;
}
*/

void * color_set(void *restrict data, const void *restrict color,
const size_t items, const size_t ch) {
	unsigned char *restrict dst = data;
	const unsigned char *restrict src = color;

	if (memchk(src + 1, src[0], ch - 1)) {
		memset(dst, src[0], items * ch);
	} else {
		for (size_t i = 0; i < items; ++i) {
			dst[i*ch] = src[0];
			if (ch == 2) {
				dst[i*ch + 1] = src[1];
			} else if (ch == 3) {
				dst[i*ch + 1] = src[1];
				dst[i*ch + 2] = src[2];
			} else if (ch == 4) {
				dst[i*ch + 1] = src[1];
				dst[i*ch + 2] = src[2];
				dst[i*ch + 3] = src[3];
			}
		}
	}
	return dst + (items * ch);
}

/*
void composite_color(struct raw_img *img, const unsigned char *restrict color,
const struct anim_frame *frame) {
	unsigned char *pos = img->data
		+ ((frame->y * img->w + frame->x) * img->channels);

	if (frame->w == img->w) {
		color_set(pos, color, img->w * img->h, img->channels);
	} else {
		for (size_t i = 0; i < frame->h; ++i) {
			color_set(pos, color, img->w, img->channels);
			pos += img->w * img->channels;
		}
	}
}*/

void composite_clear(struct raw_img *img, const struct anim_frame *frame) {
	unsigned char *pos = img->data
		+ ((frame->y * img->w + frame->x) * img->channels);

//	if (frame->w == img->w) {
//		memset(pos, 0, frame->w * frame->h * img->channels);
//	} else {
		for (size_t i = 0; i < frame->h; ++i) {
			memset(pos, 0, frame->w * img->channels);
			pos += img->w * img->channels;
		}
//	}
}

void copy_unaffected(struct raw_img *img, const unsigned char *restrict prev,
const struct anim_frame *frame) {
	size_t offset = (frame->y * img->w + frame->x) * img->channels;
	const size_t copy_stride = (img->w - frame->w) * img->channels;
	const size_t tail = (img->w * img->h - (frame->h - 1) * img->w
		- frame->w) * img->channels - offset;

	memcpy(img->data, prev, offset);
//	if (copy_stride == 0) {
//		offset += frame->w * frame->h * img->channels;
//	} else {
		offset += frame->w * img->channels;
		for (size_t i = 0; i < frame->h - 1; ++i) {
			memcpy(img->data + offset, prev + offset, copy_stride);
			offset += img->w * img->channels;
		}
//	}
	memcpy(img->data + offset, prev + offset, tail);
}
