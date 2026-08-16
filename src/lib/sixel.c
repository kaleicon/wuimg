// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include "sixel.h"
#include "misc/common.h"
#include "misc/math.h"
#include "raster/fmt.h"

/* Reference:
https://vt100.net/docs/vt3xx-gp/chapter14.html
*/

static const size_t SIXEL_LINE_HEIGHT = 6;

struct sixel_colormap {
	struct pix_rgba8 active;
	struct palette map;
};

enum sixel_control_character {
	ansi_escape = 0x1b,
	graphics_repeat_introducer = '!',
	raster_attributes = '"',
	color_introducer = '#',
	graphics_carriage_return = '$',
	graphics_new_line = '-',
	device_control_string = 0x90,
	string_terminator = 0x9c,
};

enum sixel_colorspace {
	sixel_hls = '1',
	sixel_rgb = '2',
};

static bool issixel(int c) {
	return c >= '?' && c <= '~';
}

static int32_t i32min(const int32_t x, const int32_t y) {
	return x < y ? x : y;
}
static int32_t i32max(const int32_t x, const int32_t y) {
	return x > y ? x : y;
}
static int32_t i32clamp(const int32_t n, const int32_t min, const int32_t max) {
	return i32min(i32max(n, min), max);
}

static int32_t hls_to_rgb(const int32_t n,
const int32_t h, const int32_t l, const int32_t s, const int32_t point) {
	// https://en.wikipedia.org/wiki/HLS_color_space#HSL_to_RGB_alternative
	const int32_t k = (n + h / 30) % (12 * point);
	const int32_t a = s * i32min(l, point - l) / point;
	const int32_t k3 = i32min(k - 3*point, 9*point - k);
	return l - (a * i32clamp(k3, -point, point))/point;
}

static void sixel_normalize_color(struct pix_rgba8 *entry,
uint32_t comp[static 3], const enum sixel_colorspace pu) {
	unsigned char *rgba = (unsigned char *)entry;

	const uint32_t point = 1 << 8; // Fixed point value of 1
	uint32_t scale;
	switch (pu) {
	case sixel_hls:
		/* H stays in range 0-360, while S and L are mapped from
		 * 0-100 to 0-1. */
		scale = (point * point) / 100 + 1;
		int32_t h = (int32_t)(comp[0] * point);
		int32_t l = (int32_t)((comp[1] * scale) / point);
		int32_t s = (int32_t)((comp[2] * scale) / point);
		for (uint32_t i = 0; i < 3; ++i) {
			const int32_t n = (int32_t)((8 - i*4)*point);
			const int32_t x = hls_to_rgb(n, h, l, s, (int32_t)point);
			rgba[i] = (uint8_t)((uint32_t)x * UCHAR_MAX / point);
		}
		break;
	case sixel_rgb:
		scale = (UCHAR_MAX * point) / 100 + 1;
		for (size_t i = 0; i < 3; ++i) {
			rgba[i] = (unsigned char)((comp[i] * scale) / point);
		}
		break;
	}
}

static void sixel_read_color(struct mparser *tp, struct sixel_colormap *map) {
	uintmax_t idx;
	mp_scan_uint_unsafe(tp, &idx);
	if (mp_next_char_unsafe(tp) == ';') {
		enum sixel_colorspace pu = mp_next_char_unsafe(tp);
		uint32_t tmp[3] = {0};
		for (size_t i = 0; i < ARRAY_LEN(tmp); ++i) {
			++tp->pos;
			uintmax_t t;
			mp_scan_uint_unsafe(tp, &t);
			tmp[i] = (uint32_t)t;
		}
		sixel_normalize_color(map->map.color + idx, tmp, pu);
	} else {
		--tp->pos;
	}
	map->active = map->map.color[idx];
}

static bool sixel_uint_check(struct mparser *tp, size_t digits, uintmax_t max_val) {
	uintmax_t out;
	size_t d = mp_scan_uint(tp, digits, &out);
	return d > 0 && d < digits && out <= max_val;
}

static bool sixel_validate_color(struct mparser *tp) {
	/* Format:
	 * (select color entry) '#' Pc
	 * (set color value)    '#' Pc ; Pu ; Px ; Py ; Pz
	 * Pc is the color index, in range 0-255.
	 * Pu is the color space, either 1 (HLS) or 2 (RGB). Required.
	 * Px is the first component
	 *    in range 0-360 if HLS.
	 *    in range 0-100 if RGB.
	 * Py and Pz are the second and third components, in range 0-100.
	 * All three components are zero if omitted. Pc is left as the active
	 * color in both cases.
	 * HLS convention is:
	 *    Blue:  H=0,   L=50, S=100
	 *    Red:   H=120, L=50, S=100
	 *    Green: H=240, L=50, S=100
	*/
	const size_t max_digits = 4;
	if (!sixel_uint_check(tp, max_digits, UCHAR_MAX)) {
		return false;
	}
	int c = mp_next_char(tp);
	if (c == ';') {
		c = mp_next_char(tp);
		unsigned first_max;
		switch (c) {
		case sixel_hls: first_max = 360; break;
		case sixel_rgb: first_max = 100; break;
		default: return false;
		}
		for (size_t i = 0; i < 3; ++i) {
			if (mp_next_char(tp) != ';') {
				return false;
			}
			const unsigned max = (i == 0) ? first_max : 100;
			if (!sixel_uint_check(tp, max_digits, max)) {
				return false;
			}
		}
	} else {
		--tp->pos;
	}
	return c != EOF;
}

static void sixel_write_color(struct pix_rgba8 *dst,
const struct sixel_colormap *map, const size_t w, unsigned char sixel,
const size_t len) {
	sixel -= '?';
	for (size_t y = 0; y < SIXEL_LINE_HEIGHT; ++y) {
		if ((sixel >> y) & 1) {
			for (size_t x = 0; x < len; ++x) {
				dst[w*y + x] = map->active;
			}
		}
	}
}

static void xterm_colormap_init(struct sixel_colormap *map) {
	// xterm ANSI colors as seen on XTerm-col.ad
	*map = (struct sixel_colormap) {
		.map.color = {
			{0,   0,   0  , 0xff}, // black
			{205, 0,   0  , 0xff}, // red3
			{0,   205, 0  , 0xff}, // green3
			{205, 205, 0  , 0xff}, // yellow3
			{0,   0,   238, 0xff}, // blue2
			{205, 0,   205, 0xff}, // magenta3
			{0,   205, 205, 0xff}, // cyan3
			{229, 229, 229, 0xff}, // gray90

			{127, 127, 127, 0xff}, // gray50
			{255, 0,   0  , 0xff}, // red
			{0,   255, 0  , 0xff}, // green
			{255, 255, 0  , 0xff}, // yellow
			{92,   92, 255, 0xff}, // rgb:5c/5c/ff
			{255, 0,   255, 0xff}, // magenta
			{0,   255, 255, 0xff}, // cyan
			{255, 255, 255, 0xff}, // white
		}
	};

	unsigned char cube[6];
	cube[0] = 0;
	for (size_t i = 1; i < ARRAY_LEN(cube); ++i) {
		cube[i] = (unsigned char)(0x37 + 0x28 * i);
	}

	struct pix_rgba8 *pal = map->map.color;
	map->active = pal[0];
	size_t pos = 16;
	for (size_t r = 0; r < ARRAY_LEN(cube); ++r) {
		for (size_t g = 0; g < ARRAY_LEN(cube); ++g) {
			for (size_t b = 0; b < ARRAY_LEN(cube); ++b) {
				pal[pos].r = cube[r];
				pal[pos].g = cube[g];
				pal[pos].b = cube[b];
				pal[pos].a = 0xff;
				++pos;
			}
		}
	}

	for (uint8_t gray = 0x08; pos < ARRAY_LEN(map->map.color); ++pos) {
		pal[pos].r = gray;
		pal[pos].g = gray;
		pal[pos].b = gray;
		pal[pos].a = 0xff;
		gray = (uint8_t)(gray + 0x0a);
	}
}

struct wu_st sixel_decode(const struct sixel_desc *desc, struct wuimg *img) {
	struct pix_rgba8 *dst = (struct pix_rgba8 *)img->data;

	struct sixel_colormap map;
	xterm_colormap_init(&map);

	struct mparser tp = desc->tp;
	size_t x = 0;
	size_t y = 0;
	// We've already validated the data so we can omit most checks.
	while (tp.pos < tp.len) {
		size_t line = y*img->w;
		unsigned char c = mp_next_char_unsafe(&tp);
		switch (c) {
		case graphics_new_line:
			y += SIXEL_LINE_HEIGHT;
			// fallthrough
		case graphics_carriage_return:
			x = 0;
			break;
		case graphics_repeat_introducer:
			;uintmax_t repeat;
			mp_scan_uint_unsafe(&tp, &repeat);
			c = mp_next_char_unsafe(&tp);

			const size_t pixs = (size_t)repeat;
			sixel_write_color(dst + line + x, &map, img->w, c, pixs);
			x += pixs;
			break;
		case color_introducer:
			sixel_read_color(&tp, &map);
			break;
		case '\n': case '\r':
			break;
		default:
			sixel_write_color(dst + line + x, &map, img->w, c, 1);
			++x;
		}
	}
	return WU_OK;
}

static struct wu_st sixel_calc_dimensions(struct sixel_desc *desc,
struct wuimg *img) {
	/* We must do a pass over the whole stream to know the image
	 * dimensions. No other way around it. */
	struct mparser tp = desc->tp; // Local copy
	size_t height = 0;
	size_t row_width = 0;
	bool partial_line = false; /* Keep track of whether the latest line
		will be written to. row_width is not reliable for that, as
		graphics_carriage_return may set it to 0 just at the end. */

	const char *msg = NULL;
	bool end = false;
	size_t data_end;
	do {
		data_end = tp.pos;
		const int c = mp_next_char(&tp);
		switch (c) {
		case EOF:
			msg = "missing String Terminator (stream is truncated)";
			break;
		case ansi_escape:
			end = true;
			break;
		case graphics_new_line:
			partial_line = false;
			++height;
			// fallthrough
		case graphics_carriage_return:
			if (row_width > img->w) {
				img->w = row_width;
			}
			row_width = 0;
			break;
		case graphics_repeat_introducer:
			;uintmax_t repeat;
			const size_t max_digits = 5;
			size_t d = mp_scan_uint(&tp, max_digits, &repeat);
			if (d > 0 && d < max_digits) {
				if (issixel(mp_next_char(&tp))) {
					row_width += (size_t)repeat;
					partial_line = true;
				} else {
					msg = "stopping at non-sixel character";
				}
			} else {
				msg = "stopping at bad decimal";
			}
			break;
		case color_introducer:
			if (!sixel_validate_color(&tp)) {
				msg = "stopping at bad color";
			}
			break;
		case '\n': case '\r':
			break;
		default:
			if (c >= 0x80) {
				end = true;
			} else { //if (issixel(c)) {
				/* boticelli.six repeatedly uses '>',
				 * so render whatever and keep going I guess */
				++row_width;
				partial_line = true;
			}
		}
	} while (!end && !msg);

	if (row_width > img->w) {
		img->w = row_width;
	}
	if (img->w) {
		height = (height + partial_line) * SIXEL_LINE_HEIGHT;
		if (height > img->h) {
			img->h = height;
		}
		desc->tp.len = data_end;
		return wuerr(wu_ok, msg);
	}
	return wuerr(wu_decoding_error, msg);
}

static struct wu_st sixel_get_raster_attr(struct mparser *tp,
unsigned int *raster, const size_t len, size_t *parsed) {
	/* Format: '"' Pan ; Pad ; Ph ; Pv
	 * Pan (aspect numerator) is the vertical aspect ratio. Required.
	 * Pad (aspect denominator) is the horizontal aspect ratio. Required.
	 * Ph is the horizontal image size in pixels. Optional.
	 * Pv in the vertical size. Optional. */
	*parsed = 0;
	while (*parsed < len) {
		uintmax_t val;
		mp_scan_uint(tp, 5, &val);
		raster[*parsed] = (unsigned)val;
		++*parsed;
		const int c = mp_next_char(tp);
		switch (c) {
		case ';': break;
		case EOF: return WUERR_HERE(wu_unexpected_eof);
		default:
			--tp->pos;
			return *parsed >= 2
				? WU_OK
				: wuerr(wu_invalid_header, "not enough fields"
					" in raster attribute string");
		}
	}
	return wuerr(wu_invalid_header, "too many attributes");
}

static struct wu_st dcs_parse(struct mparser *tp,
unsigned *macro, const size_t len) {
	/* Format (after DCS): P1 ; P2 ; P3 ; 'q'
	 * P1 is the pixel vertical aspect ratio, in range 0-9. 0 if omitted.
	 * P2 is whether 0 pixels are set to the background color or not
	 *     modified. In range 0-2.
	 * P3 is the horizontal grid size, the distance between two pixels.
	 *     No idea how/if this influences anything.
	 * Any of these components may be omitted. The last semicolon may or
	 * may not be present. */
	int c;
	for (size_t i = 0; i < len; ++i) {
		uintmax_t val;
		mp_scan_uint(tp, 1, &val);
		macro[i] = (unsigned)val;
		c = mp_next_char(tp);
		switch (c) {
		case ';': break;
		case 'q': return WU_OK;
		case EOF: return WUERR_HERE(wu_unexpected_eof);
		default: return wuerr(wu_invalid_header, "bad DCS character");
		}
	}
	c = mp_next_char(tp);
	switch (c) {
	case 'q': return WU_OK;
	case EOF: return WUERR_HERE(wu_unexpected_eof);
	}
	return wuerr(wu_invalid_header, "badly terminated DCS");
}

static struct wu_st sixel_calc_parameters(struct sixel_desc *desc,
struct wuimg *img) {
	struct mparser *tp = &desc->tp;

	unsigned macro[3] = {0};
	struct wu_st status = dcs_parse(tp, macro, ARRAY_LEN(macro));
	if (!wu_isok(status)) {
		return status;
	}

	unsigned pan = 2; // Vertical size
	unsigned pad = 1; // Horizontal size
	switch (macro[0]) {
	case 2:
		pan = 5;
		break;
	case 3: case 4:
		pan = 3;
		break;
	case 7: case 8: case 9:
		pan = 1;
		break;
	case 0: case 1: case 5: case 6:
		pan = 2;
		break;
	default:
		return wuerr(wu_invalid_header, "DCS aspect ratio out of range");
	}
	switch (macro[1]) {
	case 0: case 2:
		desc->p2 = sixel_set_to_bg;
		break;
	case 1:
		desc->p2 = sixel_retain;
		break;
	default:
		return wuerr(wu_invalid_header, "bad DCS background setting");
	}
	desc->horizontal_grid_size = macro[2];

	const int c = mp_next_nonspace(tp);
	if (c == raster_attributes) {
		size_t parsed;
		unsigned raster[4];
		status = sixel_get_raster_attr(tp, raster, ARRAY_LEN(raster),
			&parsed);
		if (!wu_isok(status)) {
			return status;
		} else if (!raster[0] || !raster[1]) {
			return wuerr(wu_invalid_header,
				"aspect ratio num and den must be != 0");
		}
		pan = raster[0];
		pad = raster[1];
		if (parsed == ARRAY_LEN(raster)) {
			img->w = raster[2];
			img->h = raster[3];
		}
	} else if (c == EOF) {
		return WUERR_HERE(wu_unexpected_eof);
	} else {
		--tp->pos;
	}
	img->channels = 4;
	img->bitdepth = 8;
	wuimg_aspect_ratio(img, pad, pan);
	return sixel_calc_dimensions(desc, img);
}

struct wu_st sixel_try_parse(struct sixel_desc *desc, struct wuimg *img,
const struct wuptr mem, size_t dcs_search_limit) {
	/* A sixel image begins with the Device Control String, which might
	 * come in single-byte and two-byte form. It's basically a giant
	 * terminal command written to a file, and may be preceded by text or
	 * other terminal stuff. */
	const size_t limit = zumin(mem.len, dcs_search_limit);
	for (size_t i = 0; i < limit; ++i) {
		switch (mem.ptr[i]) {
		case ansi_escape:
			if (limit - i >= 2 && mem.ptr[i+1] == 'P') {
				++i;
		case device_control_string:
				++i;
				*desc = (struct sixel_desc) {
					.tp = mp_mem(mem.len - i, mem.ptr + i),
				};
				return sixel_calc_parameters(desc, img);
			}
		default: break;
		}
	}
	return wuerr(wu_invalid_signature, "Device Control String not found");
}
