// SPDX-License-Identifier: 0BSD
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

#define MACRO_CASE_SPACE case ' ': case '\f': case '\n': case '\r': case '\t': case '\v':
#define MACRO_CASE_DIGIT case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':

static const size_t LINE_HEIGHT = 6;

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

static unsigned char hls_to_rgb(const int_fast16_t n,
const int_fast16_t comp[static 3], const int_fast16_t point) {
	// https://en.wikipedia.org/wiki/HLS_color_space#HSL_to_RGB_alternative
	const int_fast16_t h = comp[0];
	const int_fast16_t l = comp[1];
	const int_fast16_t s = comp[2];

	const int_fast16_t k = (n + h / 30) % (12 * point);
	const int_fast16_t a = s * lmin(l, point - l) / point;
	const int_fast16_t min = lmin( lmin(k - 3*point, 9*point - k), point);
	const int_fast16_t max = lmax(-point, min);

	const int_fast16_t result = l - a * max / point;
	return (unsigned char)((result * UCHAR_MAX) / point);
}

static void normalize_color(struct pix_rgba8 *entry,
int_fast16_t comp[static 3], const enum sixel_colorspace pu) {
	unsigned char *rgba = (unsigned char *)entry;

	const int_fast16_t point = 1 << 8;
	int_fast16_t scale;
	switch (pu) {
	case sixel_hls:
		scale = (0x100 * point) / 100 + 1;
		comp[0] *= point;
		comp[1] = (comp[1] * scale) / point;
		comp[2] = (comp[2] * scale) / point;
		for (int_fast16_t i = 0; i < 3; ++i) {
			const int_fast16_t n = ((12 - i*4) % 12) * point;
			rgba[i] = hls_to_rgb(n, comp, point);
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

static void read_color(struct mparser *tp, struct sixel_colormap *map) {
	long idx;
	mp_scan_uint_unsafe(tp, &idx);
	if (mp_next_char_unsafe(tp) == ';') {
		enum sixel_colorspace pu = mp_next_char_unsafe(tp);
		long tmp[3] = {0};
		for (size_t i = 0; i < ARRAY_LEN(tmp); ++i) {
			++tp->pos;
			mp_scan_uint_unsafe(tp, tmp + i);
		}
		normalize_color(map->map.color + idx, tmp, pu);
	} else {
		--tp->pos;
	}
	map->active = map->map.color[idx];
}

static bool validate_color(struct mparser *tp) {
	/* Format:
	 * (select color entry) '#' Pc
	 * (set color value)    '#' Pc ; Pu ; Px ; Py ; Pz
	 * Pc is the color index, in range 0-255.
	 * Pu is the color space, either 1 (HLS) or 2 (RGB). Required.
	 * Px is the first component
	 *    in range 0-360 if HLS.
	 *    in range 0-100 if RGB.
	 * Py and Pz are the second and third components, in range 0-100.
	 * All three components are zero if omitted.
	 * Note that the 'set' form leaves Pc as the active color. That was
	 * a fun bug to hunt. */
	long idx;
	if (!mp_scan_uint(tp, 3, &idx) || idx > UCHAR_MAX) {
		return false;
	}
	if (mp_next_char(tp) == ';') {
		const int c = mp_next_char(tp);
		enum sixel_colorspace pu;
		switch (c) {
		case sixel_hls: case sixel_rgb:
			pu = (enum sixel_colorspace)c;
			break;
		default:
			return false;
		}
		for (size_t i = 0; i < 3; ++i) {
			if (mp_next_char(tp) != ';') {
				return false;
			}
			const long max =
				(i == 0 && pu == sixel_hls) ? 360 : 100;
			long val;
			if (!mp_scan_uint(tp, 3, &val) || val > max) {
				return false;
			}
		}
	} else {
		--tp->pos;
	}
	return true;
}

static void write_color(struct pix_rgba8 *dst, const struct sixel_colormap *map,
const size_t w, unsigned char sixel, const size_t len) {
	sixel -= '?';
	for (size_t y = 0; y < LINE_HEIGHT; ++y) {
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

size_t sixel_decode(const struct sixel_desc *desc, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}

	struct pix_rgba8 *dst = (struct pix_rgba8 *)img->data;

	struct sixel_colormap map;
	xterm_colormap_init(&map);

	struct mparser tp = (struct mparser) {
		.mem = desc->tp.mem,
		.len = desc->data_end,
		.pos = desc->tp.pos,
	};
	size_t x = 0;
	size_t y = 0;
	// We've already validated the data so we can omit most checks.
	while (tp.pos < tp.len) {
		size_t line = y*img->w;;
		unsigned char c = mp_next_char_unsafe(&tp);
		switch (c) {
		case graphics_new_line:
			y += LINE_HEIGHT;
			// fallthrough
		case graphics_carriage_return:
			x = 0;
			break;
		case graphics_repeat_introducer:
			;long repeat;
			mp_scan_uint_unsafe(&tp, &repeat);
			c = mp_next_char_unsafe(&tp);

			const size_t pixs = (size_t)repeat;
			write_color(dst + line + x, &map, img->w, c, pixs);
			x += pixs;
			break;
		case color_introducer:
			read_color(&tp, &map);
			break;
		MACRO_CASE_SPACE
			break;
		default:
			write_color(dst + line + x, &map, img->w, c, 1);
			++x;
		}
	}
	return y*img->w + x;
}

static enum wu_error calc_dimensions(struct sixel_desc *desc,
struct wuimg *img) {
	/* We must do a pass over the whole stream to know the image
	 * dimensions. No other way around it. */
	size_t row_width = 0;
	bool partial_line = false; /* Keep track of whether the latest line
		will be written to. row_width is not reliable for that, as
		graphics_carriage_return may set it to 0 just at the end. */
	size_t height = 0;
	struct mparser tp = desc->tp; // Local copy
	for (bool end = false; !end;) {
		const int c = mp_next_char(&tp);
		switch (c) {
		case EOF:
			return wu_decoding_error;
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
			;long repeat;
			if (!mp_scan_uint(&tp, 5, &repeat)) {
				return wu_decoding_error;
			}

			if (!issixel(mp_next_char(&tp))) {
				return wu_decoding_error;
			}
			row_width += (size_t)repeat;
			partial_line = true;
			break;
		case color_introducer:
			if (!validate_color(&tp)) {
				return wu_decoding_error;
			}
			break;
		MACRO_CASE_SPACE
			break;
		default:
			if (issixel(c)) {
				++row_width;
				partial_line = true;
			} else if (c >= 0x80) {
				end = true;
			} else {
				return wu_decoding_error;
			}
		}
	}

	if (row_width > img->w) {
		img->w = row_width;
	}
	if (img->w) {
		height = (height + partial_line) * LINE_HEIGHT;
		if (height > img->h) {
			img->h = height;
		}
		desc->data_end = tp.pos - 1;
		return wuimg_verify(img);
	}
	return wu_decoding_error;
}

static enum wu_error get_raster_attributes(struct mparser *tp,
unsigned int raster[4]) {
	/* Format: '"' Pan ; Pad ; Ph ; Pv
	 * Pan (aspect numerator) is the vertical aspect ratio. Required.
	 * Pad (aspect denominator) is the horizontal aspect ratio. Required.
	 * Ph is the horizontal image size in pixels. Optional.
	 * Pv in the vertical size. Optional. */
	for (size_t i = 0; i < 4;) {
		const int c = mp_next_char(tp);
		switch (c) {
		MACRO_CASE_DIGIT
			;const unsigned prev = raster[i];
			raster[i] = raster[i] * 10 - '0' + (unsigned)c;
			if (raster[i] < prev) {
				return wu_int_overflow;
			}
			break;
		case ';':
			++i;
			break;
		case EOF:
			return wu_unexpected_eof;
		default:
			if (i < 2) {
				return wu_invalid_header;
			}
			--tp->pos;
			return wu_ok;
		}
	}
	// Can't have more than three colons
	return wu_invalid_header;
}

static enum wu_error dcs_parse(struct mparser *tp,
unsigned char macro[3]) {
	int num_len = 0;
	for (size_t i = 0; i < 3;) {
		int c = mp_next_char(tp);
		switch (c) {
		MACRO_CASE_DIGIT
			if (num_len > 0) {
				return wu_invalid_header;
			}
			macro[i] = (unsigned char)(c - '0');
			++num_len;
			break;
		case ';':
			num_len = 0;
			++i;
			break;
		case 'q':
			return wu_ok;
		case EOF:
			return wu_unexpected_eof;
		default:
			return wu_invalid_header;
		}
	}
	return (mp_next_char(tp) == 'q') ? wu_ok : wu_invalid_header;
}

enum wu_error sixel_calc_parameters(struct sixel_desc *desc,
struct wuimg *img) {
	/* Format (after DCS): P1 ; P2 ; P3 ; 'q'
	 * P1 is the pixel vertical aspect ratio, in range 0-9. 2 if omitted.
	 * P2 is whether 0 pixels are set to the background color or not
	 *     modified. In range 0-2.
	 * P3 is the horizontal grid size, the distance between two pixels.
	 *     I don't know its range.
	 * Any of these components may be omitted. */
	struct mparser *tp = &desc->tp;

	unsigned char macro[3] = {0};
	enum wu_error status = dcs_parse(tp, macro);
	if (status != wu_ok) {
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
		return wu_invalid_header;
	}
	switch (macro[1]) {
	case 0: case 2:
		desc->p2 = sixel_set_to_bg;
		break;
	case 1:
		desc->p2 = sixel_retain;
		break;
	default:
		return wu_invalid_header;
	}
	desc->horizontal_grid_size = macro[2];

	const int c = mp_next_nonspace(tp);
	if (c == raster_attributes) {
		unsigned int raster[4] = {0};
		status = get_raster_attributes(tp, raster);
		if (status != wu_ok) {
			return status;
		} else if (raster[0] == 0 || raster[1] == 0) {
			return wu_invalid_header;
		}
		pan = raster[0];
		pad = raster[1];
		img->w = raster[2];
		img->h = raster[3];
	} else if (c == EOF) {
		return wu_unexpected_eof;
	} else {
		--tp->pos;
	}
	img->channels = 4;
	img->bitdepth = 8;
	wuimg_aspect_ratio(img, pad, pan);
	return calc_dimensions(desc, img);
}

static int skip_csi(struct mparser *tp) {
	const int max_chars = 12;
	bool prev_escape = true;
	int c = 0;
	for (int i = 0; i < max_chars; ++i) {
		c = mp_next_char(tp);
		if (prev_escape) {
			if (c == 'P') {
				return c;
			}
			prev_escape = false;
		} else if (c == ansi_escape) {
			prev_escape = true;
		}
	}
	return c;
}

enum wu_error sixel_open_mem(struct sixel_desc *desc,
const struct map_info *map) {
	desc->tp = mp_map(*map);
	struct mparser *tp = &desc->tp;

	/* The sixel format begins with the Device Control String, which might
	 * come in single-byte and two-byte form. And since it is basically a
	 * giant terminal command written to a file, some escape codes can be
	 * expected before that. */
	bool valid = false;
	int c = mp_next_char(tp);
	if (c == ansi_escape) {
		c = skip_csi(tp);
		valid = (c == 'P');
	} else if (c == device_control_string) {
		valid = true;
	}

	if (valid) {
		return wu_ok;
	} else if (c == EOF) {
		return wu_unexpected_eof;
	}
	return wu_invalid_signature;
}
