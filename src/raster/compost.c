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

static inline void blend_pixel(uint8_t *restrict d, const uint8_t *restrict s,
const uint8_t ch) {
	// Unassociated alpha blending in non-linear light.
	const uint8_t a = ch - 1;
	switch (s[a]) {
	case 0xff: // dst_a * (1 - src_a / 255) == 0
		memcpy(d, s, ch);
		return;
	case 0x00: // blend_a == 0
		return;
	}

	if (REFERENCE_BLEND) {
		const float sa = s[a];
		const float da = d[a];
		const float bb = da * (1 - sa/255.f);
		const float blend_a = sa + bb;
		for (uint8_t k = 0; k < a; ++k) {
			d[k] = (uint8_t)lroundf(
				(s[k]* sa + d[k] * bb) / blend_a
			);
		}
		d[a] = (uint8_t)lroundf(blend_a);
	} else {
		float sa = s[a];
		float da = d[a];
		float bb = fm_fmaf(sa * (-1/255.f), da, da);
		const float blend_a = sa + bb;
		for (uint8_t k = 0; k < a; ++k) {
			float c = fm_fmaf(s[k], sa, d[k] * bb);
			d[k] = (uint8_t)(ch == 2
				? (c/blend_a + .5f)
				: fm_fmaf(c, 1.0f/blend_a, .5f)
			);
		}
		d[a] = (uint8_t)(blend_a + .5f);
	}
}

static void blend_ga_pixel(uint8_t *restrict d, const uint8_t *restrict s) {
	blend_pixel(d, s, 2);
}
static void blend_rgba_pixel(uint8_t *restrict d, const uint8_t *restrict s) {
	blend_pixel(d, s, 4);
}

static void blend_row(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t len, const size_t ch) {
	for (size_t j = 0; j < len; ++j) {
		uint8_t *d = dst + j*ch;
		const uint8_t *s = src + j*ch;
		switch (ch) {
		case 2: blend_ga_pixel(d, s); break;
		case 4: blend_rgba_pixel(d, s); break;
		}
	}
}

void compost_alpha_blend(const struct compost *reg, const struct wuimg *img,
const uint8_t *restrict src) {
	const uint8_t ch = img->channels;
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

void compost_overwrite_add_alpha(const struct compost *reg,
const struct wuimg *img, const uint8_t *restrict src) {
	const uint8_t ch = img->channels;
	const uint8_t sch = ch-1;
	const size_t stride = wuimg_stride(img);
	for (size_t y = 0; y < reg->h; ++y) {
		uint8_t *d = img->data + canvas_off(stride, ch, reg, y);
		const uint8_t *s = src + frame_off(sch, reg, y);
		for (size_t x = 0; x < reg->w; ++x) {
			switch (ch) {
			case 2:
				d[x*ch] = s[x];
				d[x*ch+1] = 0xff;
				break;
			case 4:
				memcpy(d + x*ch, s + x*sch,
					(x + 1 < reg->w) ? 4 : 3);
				d[x*ch + 3] = 0xff;
				break;
			}
		}
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
