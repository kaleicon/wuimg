#include <stdint.h>
#include <math.h>

#include "wudefs.h"
#include "common.h"
#include "common_unpack.h"

struct frame_desc {
	const size_t w, h;
	const unsigned int ch;
	const struct swizzle {
		unsigned char r, b, g, a;
	} swz;
	const size_t stride, padding;
	const size_t hstep, vstep;
	const size_t tail;
};

typedef int_fast32_t ifast_t;

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

static int normalize_float(float out[3], const int depth,
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
		const ifast_t r = ct[i].r / cnt,
			g = ct[i].g / cnt,
			b = ct[i].b / cnt;
		const ifast_t min = rgb_min(r, g, b);
		const ifast_t max = rgb_max(r, g, b);

		return max * ((255 - min) / 16);
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
const u_int8_t *restrict data, const struct colormap *pal) {
	int tally[256] = {0};
	const u_int8_t *restrict imgend = data + fr->stride * fr->h;
	while (data < imgend) {
		const u_int8_t *restrict rowend = data + fr->w;
		while (data < rowend) {
			++tally[*data];
			data += fr->hstep;
		}
		data += fr->tail;
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

static void average_common8(const struct frame_desc *fr,
const u_int8_t *restrict data, struct color_tally *ct) {
	const u_int8_t *restrict imgend = data + fr->stride * fr->h;
	while (data < imgend) {
		const u_int8_t *restrict rowend = data + fr->w * fr->ch;
		while (data < rowend) {
			if (fr->ch % 2 || data[fr->swz.a]) {
				switch (fr->ch) {
				case 4: case 3:
					ct->r += data[fr->swz.r];
					ct->g += data[fr->swz.g];
					ct->b += data[fr->swz.b];
					break;
				case 2: case 1:
					ct->r += data[fr->swz.r];
					break;
				}
				++ct->cnt;
			}
			data += fr->ch * fr->hstep;
		}
		data += fr->tail;
	}
}

static void category_common8(const struct frame_desc *fr,
const u_int8_t *restrict data, struct color_tally *tally) {
	const u_int8_t *restrict imgend = data + fr->stride * fr->h;
	while (data < imgend) {
		const u_int8_t *restrict rowend = data + fr->w * fr->ch;
		while (data < rowend) {
			if (fr->ch % 2 || data[fr->swz.a]) {
				const ifast_t sr = data[fr->swz.r];
				const ifast_t sg = data[fr->swz.g];
				const ifast_t sb = data[fr->swz.b];

				struct color_tally *t = tally;
				const ifast_t mask = 0xc0;
				t += (sb & mask) >> 2 | (sg & mask) >> 4
					| sr >> 6;

				++t->cnt;
				t->r += sr;
				t->g += sg;
				t->b += sb;
			}
			data += fr->ch * fr->hstep;
		}
		data += fr->tail;
	}
}

static int get_average(float out[3], const struct frame_desc *fr,
const void *restrict data, const unsigned char bitdepth) {
	struct color_tally tally = {0};
	switch (bitdepth) {
	case 8:
		average_common8(fr, data, &tally);
		break;
	}
	return normalize_float(out, bitdepth, &tally, fr->ch);
}

static int get_category(const enum background_source src, float out[3],
const struct frame_desc *fr, const u_int8_t *restrict data,
const unsigned char bitdepth) {
	const size_t bits = 2;
	const size_t len = 1U << (bits * 3);
	struct color_tally *tally = calloc(sizeof(*tally), len);
	if (!tally) {
		return 0;
	}

	switch (bitdepth) {
	case 8:
		category_common8(fr, data, tally);
		break;
	default:
		free(tally);
		return 0;
	}

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
	const enum pix_layout lay = img->layout;
	const size_t scanline = scanline_length(img->w * img->channels,
		img->bitdepth, img->alignment);
	const size_t padding = scanline - img->w * img->channels;
	const struct frame_desc frame = {
		.w = img->w,
		.h = img->h,
		.ch = img->channels,
		.swz = {
			.r = (lay >> 6) & 0x03,
			.g = (lay >> 4) & 0x03,
			.b = (lay >> 2) & 0x03,
			.a = lay & 0x03,
		},
		.stride = scanline,
		.padding = padding,
		.hstep = img->w/maxres + 1,
		.vstep = img->h/maxres + 1,
		.tail = img->h/maxres * scanline + padding,
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
		// Fallthrough
	case average:
		return get_average(out, &frame, img->data, img->bitdepth);
	default:
		return 0;
	}
}
