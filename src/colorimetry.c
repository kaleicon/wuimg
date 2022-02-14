#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>

#include "wudefs.h"
#include "common.h"
#include "raster/pix.h"

typedef int_fast32_t comp_t;

struct frame_desc {
	const size_t w, h;
	const size_t stride;
	const size_t hstep, vstep;
	const uint8_t ch;
	uint8_t swz[4];
};

enum count_op {
	count_average,
	count_category,
};

struct color_tally {
	comp_t cnt;
	comp_t r, g, b;
};

static comp_t rgb_max(const comp_t r, const comp_t g, const comp_t b) {
	return lmax(r, lmax(g, b));
}

static comp_t rgb_min(const comp_t r, const comp_t g, const comp_t b) {
	return lmin(r, lmin(g, b));
}

static int normalize_float(float out[3], const struct color_tally *ct,
const size_t ch) {
	const comp_t cnt = ct->cnt;
	if (cnt) {
		const comp_t range = UCHAR_MAX;
		const float norm = (float)(cnt * range);

		out[0] = (float)ct->r / norm;
		out[1] = (ch < 3) ? out[0] : ((float)ct->g / norm);
		out[2] = (ch < 3) ? out[0] : ((float)ct->b / norm);
	} else {
		out[0] = 0;
		out[1] = 0;
		out[2] = 0;
	}
	return (int)cnt;
}

static comp_t vibtest(const struct color_tally *ct, const size_t i) {
	const comp_t cnt = ct[i].cnt;
	if (cnt) {
		const comp_t r = ct[i].r,
			g = ct[i].g,
			b = ct[i].b;
		const comp_t min = rgb_min(r, g, b);
		const comp_t max = rgb_max(r, g, b);
		return (max - min) / cnt;
	}
	return 0;
}

static comp_t fittest(const enum background_source src,
const struct color_tally *ct, const size_t i) {
	switch (src) {
	case bg_popular: return ct[i].cnt;
	case bg_vibrant: return vibtest(ct, i);
	default: return 0;
	}
}

static const struct color_tally * pick_category(const enum background_source src,
const struct color_tally *ct, size_t len) {
	size_t idx = 0;
	comp_t best = 0;
	for (size_t i = 1; i < len; ++i) {
		const comp_t quality = fittest(src, ct, i);
		if (quality > best) {
			best = quality;
			idx = i;
		}
	}
	return ct + idx;
}

static int palette_mostpop(float out[static 3], const struct frame_desc *fr,
const uint8_t *restrict data, const struct raster_pal *pal) {
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
	out[0] = pal->color[idx].r / 255.0f;
	out[1] = pal->color[idx].g / 255.0f;
	out[2] = pal->color[idx].b / 255.0f;
	return (int)((fr->w / fr->hstep) * (fr->h / fr->vstep));
}

static void add_count(const struct frame_desc *fr, const uint8_t *restrict pix,
struct color_tally *tally, const enum count_op op) {
	switch (op) {
	case count_average:
		switch (fr->ch) {
		case 4: case 3:
			tally->r += pix[fr->swz[pix_red]];
			tally->g += pix[fr->swz[pix_green]];
			tally->b += pix[fr->swz[pix_blue]];
			break;
		case 2: case 1:
			tally->r += pix[fr->swz[pix_red]];
			break;
		}
		++tally->cnt;
		break;
	case count_category:;
		const comp_t r = pix[fr->swz[pix_red]];
		const comp_t g = pix[fr->swz[pix_green]];
		const comp_t b = pix[fr->swz[pix_blue]];

		const comp_t mask = 0xc0;
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
		const uint8_t *restrict row = data + y * fr->stride;
		const uint8_t *restrict rowend = row + fr->w;
		while (row < rowend) {
			if (fr->ch % 2 || row[fr->swz[pix_alpha]]) {
				add_count(fr, row, tally, op);
			}
			row += fr->hstep;
		}
	}
}

static int get_average(float out[3], const struct frame_desc *fr,
const void *restrict data) {
	struct color_tally tally = {0};
	count_common(fr, data, &tally, count_average);
	return normalize_float(out, &tally, fr->ch);
}

static int get_category(const enum background_source src, float out[3],
const struct frame_desc *fr, const uint8_t *restrict data) {
	const size_t bits = 2;
	const size_t len = 1U << (bits * 3);
	struct color_tally *tally = calloc(sizeof(*tally), len);
	if (tally) {
		count_common(fr, data, tally, count_category);
		const struct color_tally *ct = pick_category(src, tally, len);
		normalize_float(out, ct, fr->ch);

		comp_t acc = 0;
		for (size_t i = 0; i < len; ++i) {
			acc += tally[i].cnt;
		}

		free(tally);
		return (int)acc;
	}
	return 0;
}

int get_image_color(float out[static 3], const struct raw_img *img,
const enum background_source src, const size_t maxres) {
	if (img->bitdepth % 8 || img->attr != pix_normal) {
		return 0;
	}

	const enum pix_layout layout = img->layout;
	const size_t scanline = scanline_length(img->w * img->channels,
		img->bitdepth, img->alignment);
	const size_t bytedepth = img->bitdepth/8;
	struct frame_desc frame = {
		.w = img->w * bytedepth * img->channels,
		.h = img->h,
		.ch = img->channels,
		.stride = scanline,
		.hstep = (img->w*bytedepth/maxres + bytedepth) * img->channels,
		.vstep = img->h/maxres + 1,
	};

	switch (img->mode) {
	case image_mode_palette:
		return palette_mostpop(out, &frame, img->data, img->u.palette);
	case image_mode_planar:
		break;
	case image_mode_raw:
		for (enum pix_color c = 0; c < pix_color_total; ++c) {
			frame.swz[c] = (uint8_t)(bytedepth
				* pix_layout_offset(layout, c));
		}

		const uint8_t *data = img->data + (bytedepth-1) * which_end();
		switch (src) {
		case bg_vibrant:
		case bg_popular:
			if (frame.ch > 2) {
				return get_category(src, out, &frame, data);
			}
			// fallthrough
		case bg_average:
			return get_average(out, &frame, data);
		default:
			break;
		}
		break;
	}
	return 0;
}
