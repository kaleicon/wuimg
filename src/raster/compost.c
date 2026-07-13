// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <string.h>

#include "misc/math.h"
#include "raster/compost.h"
#include "raster/wuimg.h"

#include "fast_math.c"

// Clear but slow alpha blend function to compare against
static const bool REFERENCE_BLEND = false;

static size_t canvas_off(size_t stride, uint8_t ch, const struct compost *reg,
size_t y) {
	return (reg->y + y) * stride + reg->x*ch;
}

static size_t frame_off(uint8_t ch, const struct compost *reg, size_t y) {
	return ch*reg->w*y;
}

static void blend_rgba_pixel(unsigned char *restrict d,
const unsigned char *restrict s) {
	/* Unassociated alpha blending.
	 * Note that libwebp does alpha blending in non-linear light.
	 * Not that we complain.
	*/
	const uint8_t ch = 4;
	switch (s[3]) {
	case 0xff: // dst_a * (1 - src_a / 255) == 0
		memcpy(d, s, ch);
		return;
	case 0x00: // blend_a == 0
		return;
	}

	if (REFERENCE_BLEND) {
		const float sa = s[3];
		const float da = d[3];
		const float bb = da * (1 - sa/255.f);
		const float blend_a = sa + bb;
		for (uint8_t k = 0; k < ch - 1; ++k) {
			d[k] = (uint8_t)lroundf(
				(s[k]* sa + d[k] * bb) / blend_a
			);
		}
		d[3] = (uint8_t)lroundf(blend_a);
	} else {
		float sa = s[3];
		float da = d[3];
		float bb = fm_fmaf(sa * (-1/255.f), da, da);
		const float blend_a = sa + bb;
		const float iba = 1/blend_a;
		for (uint8_t k = 0; k < ch - 1; ++k) {
			d[k] = (uint8_t)(
				fm_fmaf(fm_fmaf(s[k], sa, d[k] * bb), iba, .5f)
			);
		}
		d[3] = (uint8_t)(blend_a + .5f);
	}
}

static void blend_row(unsigned char *restrict dst,
const unsigned char *restrict src, const size_t len, const size_t ch) {
	for (size_t j = 0; j < len; ++j) {
		blend_rgba_pixel(dst + j*ch, src + j*ch);
	}
}

void compost_alpha_blend(const struct compost *reg, const struct wuimg *img,
const uint8_t *restrict src) {
	const uint8_t ch = 4;
	const size_t stride = wuimg_stride(img);
	for (size_t y = 0; y < reg->h; ++y) {
		blend_row(img->data + canvas_off(stride, ch, reg, y),
			src + frame_off(ch, reg, y), reg->w, ch);
	}
}

static void span_expand(uint8_t *restrict dst, const uint8_t *restrict src,
const struct palette *pal, size_t w, uint8_t ch) {
	for (size_t x = 0; x < w; ++x) {
		const uint8_t c = src[x];
		memcpy(dst + x*ch, pal->color + c, ch);
	}
}

static void pal_to_color(uint8_t *restrict dst, const uint8_t *restrict src,
const struct palette *pal, size_t w, uint8_t ch, const int idx) {
	if (idx == (uint8_t)idx) {
		for (;;) {
			const uint8_t *alpha = memchr(src, idx, w);
			if (!alpha) {
				break;
			}
			const size_t span = (size_t)(alpha - src);
			span_expand(dst, src, pal, span, ch);
			dst += (span + 1)*ch;
			src += span + 1;
			w -= span + 1;

			while (w && *src == idx) {
				dst += ch;
				++src;
				--w;
			}
		}
	}
	span_expand(dst, src, pal, w, ch);
}

void compost_pal_expand_idx_ignore(const struct compost *reg,
const struct wuimg *img, const uint8_t *restrict src, const int alpha_idx,
const struct palette *pal) {
	const uint8_t src_ch = 1;
	const uint8_t dst_ch = 4;
	const size_t stride = wuimg_stride(img);
	for (size_t y = 0; y < reg->h; ++y) {
		pal_to_color(img->data + canvas_off(stride, dst_ch, reg, y),
			src + frame_off(src_ch, reg, y), pal, reg->w, dst_ch,
			alpha_idx);
	}
}

void compost_overwrite(const struct compost *reg, const struct wuimg *img,
const uint8_t *restrict src) {
	const uint8_t ch = img->channels;
	const size_t stride = wuimg_stride(img);
	for (size_t y = 0; y < reg->h; ++y) {
		memcpy(img->data + canvas_off(stride, ch, reg, y),
			src + frame_off(ch, reg, y), reg->w * ch);
	}
}

void compost_clear(const struct compost *reg, const struct wuimg *img) {
	const uint8_t ch = img->channels;
	const size_t stride = wuimg_stride(img);
	for (size_t y = 0; y < reg->h; ++y) {
		memset(img->data + canvas_off(stride, ch, reg, y), 0, reg->w*ch);
	}
}

void compost_extract(const struct compost *reg, uint8_t *restrict dst,
const struct wuimg *img) {
	const uint8_t ch = img->channels;
	const size_t stride = wuimg_stride(img);
	for (size_t y = 0; y < reg->h; ++y) {
		memcpy(dst + frame_off(ch, reg, y),
			img->data + canvas_off(stride, ch, reg, y),
			ch*reg->w);
	}
}

void compost_affect(struct compost *restrict aa,
const struct compost *restrict bb) {
	if (aa->w && aa->h) {
		size_t x1 = zumax(aa->x + aa->w, bb->x + bb->w);
		size_t y1 = zumax(aa->y + aa->h, bb->y + bb->h);
		aa->x = zumin(aa->x, bb->x);
		aa->y = zumin(aa->y, bb->y);
		aa->w = x1 - aa->x;
		aa->h = y1 - aa->y;
	} else {
		*aa = *bb;
	}
}

bool compost_is_full(const struct compost *reg, const struct wuimg *img) {
	return !reg->x & !reg->y & (reg->w == img->w) & (reg->h == img->h);
}

bool compost_bounds_check(const struct compost *reg, const struct wuimg *img) {
	return (reg->w <= img->w) & (reg->x <= img->w - reg->w)
		& (reg->h <= img->h) & (reg->y <= img->h - reg->h);
}
