#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "wudefs.h"
#include "common.h"
#include "dec/lib/common/unpack.h"

typedef int_fast32_t ifast_t;

struct swizzle {
	uint8_t r, g, b, a;
};

struct frame_desc {
	const size_t w, h;
	const size_t ch;
	const size_t stride;
	const size_t hstep, vstep;
	const struct swizzle swz;
};

enum count_op {
	count_average,
	count_category,
};

struct color_tally {
	ifast_t cnt;
	ifast_t r, g, b;
};

static ifast_t rgb_max(const ifast_t r, const ifast_t g, const ifast_t b) {
	return lmax(r, lmax(g, b));
}

static ifast_t rgb_min(const ifast_t r, const ifast_t g, const ifast_t b) {
	return lmin(r, lmin(g, b));
}

static int normalize_float(float out[3], const unsigned int depth,
const struct color_tally *ct, const size_t ch) {
	const ifast_t cnt = ct->cnt;
	if (cnt) {
		const ifast_t range = (1 << depth) - 1;
		const float norm = (float)(cnt * range);

		out[0] = (float)ct->r / norm;
		out[1] = (ch < 3) ? out[0] : ((float)ct->g / norm);
		out[2] = (ch < 3) ? out[0] : ((float)ct->b / norm);
		return (int)cnt;
	} else {
		out[0] = 0;
		out[1] = 0;
		out[2] = 0;
	}
	return 0;
}

static ifast_t vibtest(const struct color_tally *ct, const size_t i) {
	const ifast_t cnt = ct[i].cnt;
	if (cnt) {
		const ifast_t r = ct[i].r,
			g = ct[i].g,
			b = ct[i].b;
		const ifast_t min = rgb_min(r, g, b);
		const ifast_t max = rgb_max(r, g, b);
		return (max - min) / cnt;
	}
	return 0;
}

static ifast_t fittest(const enum background_source src,
const struct color_tally *ct, const size_t i) {
	switch (src) {
	case popular: return ct[i].cnt;
	case vibrant: return vibtest(ct, i);
	default: return 0;
	}
}

static const struct color_tally * pick_category(const enum background_source src,
const struct color_tally *ct, size_t len) {
	size_t idx = 0;
	ifast_t best = 0;
	for (size_t i = 1; i < len; ++i) {
		const ifast_t quality = fittest(src, ct, i);
		if (quality > best) {
			best = quality;
			idx = i;
		}
	}
	return ct + idx;
}

static int palette_mostpop(float out[3], const struct frame_desc *fr,
const uint8_t *restrict data, const struct colormap *pal) {
	int tally[256] = {0};
	for (size_t y = 0; y < fr->h; y += fr->vstep) {
		const size_t row = y * fr->stride;
		for (size_t x = 0; x < fr->w; x += fr->hstep) {
			++tally[data[row + x]];
		}
	}

	int idx = 0;
	for (int i = 1; i < 256; ++i) {
		if (tally[i] > tally[idx]) {
			idx = i;
		}
	}
	out[0] = pal[idx].r / 255.0f;
	out[1] = pal[idx].g / 255.0f;
	out[2] = pal[idx].b / 255.0f;
	return (int)((fr->w / fr->hstep) * (fr->h / fr->vstep));
}

static void add_count(const struct frame_desc *fr, const uint8_t *restrict pix,
struct color_tally *tally, const enum count_op op) {
	switch (op) {
	case count_average:
		switch (fr->ch) {
		case 4: case 3:
			tally->r += pix[fr->swz.r];
			tally->g += pix[fr->swz.g];
			tally->b += pix[fr->swz.b];
			break;
		case 2: case 1:
			tally->r += pix[fr->swz.r];
			break;
		}
		++tally->cnt;
		break;
	case count_category:;
		const ifast_t r = pix[fr->swz.r];
		const ifast_t g = pix[fr->swz.g];
		const ifast_t b = pix[fr->swz.b];

		const ifast_t mask = 0xc0;
		struct color_tally *t = tally
			+ ((b & mask) >> 2 | (g & mask) >> 4 | r >> 6);

		t->r += r;
		t->g += g;
		t->b += b;
		++t->cnt;
		break;
	}
}

static void count_common(const struct frame_desc *fr,
const uint8_t *restrict data, struct color_tally *tally, const enum count_op op) {
	for (size_t y = 0; y < fr->h; y += fr->vstep) {
		// Using pointer arith improves performance on my machine.
		const uint8_t *restrict row = data + y * fr->stride;
		const uint8_t *restrict rowend = row + fr->w * fr->ch;
		while (row < rowend) {
			if (fr->ch % 2 || row[fr->swz.a]) {
				add_count(fr, row, tally, op);
			}
			row += fr->ch * fr->hstep;
		}
	}
}

static int get_average(float out[3], const struct frame_desc *fr,
const void *restrict data, const unsigned char bitdepth) {
	struct color_tally tally = {0};
	count_common(fr, data, &tally, count_average);
	return normalize_float(out, bitdepth, &tally, fr->ch);
}

static int get_category(const enum background_source src, float out[3],
const struct frame_desc *fr, const uint8_t *restrict data,
const unsigned char bitdepth) {
	const size_t bits = 2;
	const size_t len = 1U << (bits * 3);
	struct color_tally *tally = calloc(sizeof(*tally), len);
	if (!tally) {
		return 0;
	}

	count_common(fr, data, tally, count_category);

	ifast_t acc = 0;
	for (size_t i = 0; i < len; ++i) {
		acc += tally[i].cnt;
	}

	const struct color_tally *ct = pick_category(src, tally, len);
	normalize_float(out, bitdepth, ct, fr->ch);

	free(tally);
	return (int)acc;
}

int get_image_color(float out[3], const struct raw_img *img,
const enum background_source src, const size_t maxres) {
	switch (img->bitdepth) {
	case 8: break;
	default: return 0;
	}

	const enum pix_layout layout = img->layout;
	const size_t scanline = scanline_length(img->w * img->channels,
		img->bitdepth, img->alignment);
	const struct frame_desc frame = {
		.w = img->w,
		.h = img->h,
		.ch = img->channels,
		.stride = scanline,
		.hstep = img->w/maxres + 1,
		.vstep = img->h/maxres + 1,
		.swz = {
			.r = (layout >> 6) & 0x03,
			.g = (layout >> 4) & 0x03,
			.b = (layout >> 2) & 0x03,
			.a = layout & 0x03,
		},
	};

	if (img->palette) {
		return palette_mostpop(out, &frame, img->data,
			(struct colormap *)img->palette);
	}

	switch (src) {
	case vibrant:
	case popular:
		if (frame.ch > 2) {
			return get_category(src, out, &frame, img->data,
				img->bitdepth);
		}
		// fallthrough
	case average:
		return get_average(out, &frame, img->data, img->bitdepth);
	default:
		return 0;
	}
}
