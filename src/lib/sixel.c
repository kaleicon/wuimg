#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include "../raster/lib.h"
#include "../raster/pix.h"
#include "../raster/pal.h"
#include "../raster/text.h"
#include "../common.h"
#include "sixel.h"

#define MACRO_CASE_SPACE case ' ': case '\f': case '\n': case '\r': case '\t': case '\v':
#define MACRO_CASE_DIGIT case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':

struct sixel_colormap {
	struct pix_rgba8 active;
	struct raster_pal map;
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

static const size_t LINE_HEIGHT = 6;

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

static void read_color(struct text_parser *tp, struct sixel_colormap *map) {
	text_fast_t idx;
	text_get_uint_unsafe(tp, 3, &idx);
	if (text_next_char_unsafe(tp) == ';') {
		enum sixel_colorspace pu = text_next_char_unsafe(tp);
		text_fast_t tmp[3] = {0};
		for (size_t i = 0; i < ARRAY_LEN(tmp); ++i) {
			++tp->pos;
			text_get_uint_unsafe(tp, 3, tmp + i);
		}
		normalize_color(map->map.color + idx, tmp, pu);
	} else {
		--tp->pos;
	}
	map->active = map->map.color[idx];
}

static bool validate_color(struct text_parser *tp) {
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
	 * Note that the 'set' form leaves Pc as the active color. Crazy bug, that one. */
	text_fast_t idx;
	if (!text_get_uint(tp, 3, &idx) || idx > UCHAR_MAX) {
		return false;
	}
	if (text_next_char(tp) == ';') {
		enum sixel_colorspace pu = text_next_char(tp);
		switch (pu) {
		case sixel_hls: case sixel_rgb:
			break;
		default:
			return false;
		}
		for (size_t i = 0; i < 3; ++i) {
			if (text_next_char(tp) != ';') {
				return false;
			}
			const text_fast_t max =
				(i == 0 && pu == sixel_hls) ? 360 : 100;
			text_fast_t val;
			text_get_uint(tp, 3, &val);
			if (val > max) {
				return false;
			}
		}
	} else {
		--tp->pos;
	}
	return true;
}

static void write_color(struct pix_rgba8 *out, const struct sixel_colormap *map,
const size_t stride, unsigned char sixel, const size_t len) {
	sixel -= '?';
	for (size_t i = 0; i < LINE_HEIGHT; ++i) {
		if ((sixel >> i) & 0x01) {
			for (size_t j = 0; j < len; ++j) {
				out[stride*i + j] = map->active;
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
	};

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

	unsigned char gray = 0x08;
	while (pos < ARRAY_LEN(map->map.color)) {
		pal[pos].r = gray;
		pal[pos].g = gray;
		pal[pos].b = gray;
		pal[pos].a = 0xff;
		gray = (unsigned char)(gray + 0x0a);
		++pos;
	}
}

struct pix_rgba8 * sixel_decode(const struct sixel_desc *desc) {
	const size_t dims = desc->r.w * desc->r.h;
	struct pix_rgba8 *out = calloc(dims, sizeof(*out));
	if (!out) {
		return NULL;
	}

	struct sixel_colormap map;
	xterm_colormap_init(&map);

	struct text_parser tp = (struct text_parser) {
		.text = desc->tp.text,
		.len = desc->data_end,
		.pos = desc->tp.pos,
	};
	size_t x = 0;
	size_t y = 0;
	// We've already validated the data so we can omit most checks.
	while (tp.pos < tp.len) {
		unsigned char c = text_next_char_unsafe(&tp);
		size_t pos;
		switch (c) {
		case graphics_new_line:
			y += LINE_HEIGHT;
			// fallthrough
		case graphics_carriage_return:
			x = 0;
			break;
		case graphics_repeat_introducer:
			;text_fast_t repeat;
			text_get_uint_unsafe(&tp, 3, &repeat);
			c = text_next_char_unsafe(&tp);

			pos = y*desc->r.w;
			const size_t pixs = (size_t)repeat;
			write_color(out + pos + x, &map, desc->r.w, c, pixs);
			x += pixs;
			break;
		case color_introducer:
			read_color(&tp, &map);
			break;
		MACRO_CASE_SPACE
			break;
		default:
			pos = y*desc->r.w;
			write_color(out + pos + x, &map, desc->r.w, c, 1);
			++x;
		}
	}
	return (struct pix_rgba8 *)out;
}

static enum lib_fail calc_dimensions(struct sixel_desc *desc) {
	/* We must do a pass over the whole stream to know the image
	 * dimensions. No other way around it. */
	size_t row_width = 0;
	size_t height = LINE_HEIGHT;
	struct text_parser tp = desc->tp; // Local copy
	for (bool end = false; !end;) {
		const int c = text_next_char(&tp);
		switch (c) {
		case EOF:
			puts("SIXEL error: Ending escape byte not found.");
			return lib_invalid_data;
		case ansi_escape:
			end = true;
			break;
		case graphics_new_line:
			height += LINE_HEIGHT;
			// fallthrough
		case graphics_carriage_return:
			if (row_width > desc->r.w) {
				desc->r.w = row_width;
			}
			row_width = 0;
			break;
		case graphics_repeat_introducer:
			;text_fast_t repeat;
			if (!text_get_uint(&tp, 3, &repeat)) {
				puts("SIXEL error: Repeat introducer lacks "
					"digits.");
				return lib_invalid_data;
			}

			if (!issixel(text_next_char(&tp))) {
				puts("SIXEL error: Found non-sixel graphics "
					"repeat.");
				return lib_invalid_data;
			}
			row_width += (size_t)repeat;
			break;
		case color_introducer:
			if (!validate_color(&tp)) {
				puts("SIXEL error: Failed to parse color "
					"introducer.");
				return lib_invalid_data;
			}
			break;
		MACRO_CASE_SPACE
			break;
		default:
			if (issixel(c)) {
				++row_width;
			} else if (c >= 0x80) {
				end = true;
			} else {
				printf("SIXEL error: Found invalid character at %#zx: %d\n",
					tp.pos, c);
				return lib_invalid_data;
			}
		}
	}

	if (height > desc->r.h) {
		desc->r.h = height;
	}
	if (row_width > desc->r.w) {
		desc->r.w = row_width;
	}
	if (desc->r.w) {
		desc->data_end = tp.pos - 1;
		return lib_ok;
	}
	return lib_invalid_data;
}

static enum lib_fail get_raster_attributes(struct text_parser *tp,
unsigned int raster[4]) {
	/* Format: '"' Pan ; Pad ; Ph ; Pv
	 * Pan (aspect numerator) is the vertical aspect ratio. Required.
	 * Pad (aspect denominator) is the horizontal aspect ratio. Required.
	 * Ph is the horizontal image size in pixels. Optional.
	 * Pv in the vertical size. Optional. */
	for (size_t i = 0; i < 4;) {
		const int c = text_next_char(tp);
		switch (c) {
		MACRO_CASE_DIGIT
			;const unsigned prev = raster[i];
			raster[i] = raster[i] * 10 - '0' + (unsigned)c;
			if (raster[i] < prev) {
				return lib_int_overflow;
			}
			break;
		case ';':
			++i;
			break;
		case EOF:
			return lib_unexpected_eof;
		default:
			if (i < 2) {
				return lib_invalid_header;
			}
			--tp->pos;
			return lib_ok;
		}
	}
	// Can't have more than three colons
	return lib_invalid_header;
}

static enum lib_fail dcs_parse(struct text_parser *tp,
unsigned char macro[3]) {
	int num_len = 0;
	for (size_t i = 0; i < 3;) {
		int c = text_next_char(tp);
		switch (c) {
		MACRO_CASE_DIGIT
			if (num_len > 0) {
				return lib_invalid_header;
			}
			macro[i] = (unsigned char)(c - '0');
			++num_len;
			break;
		case ';':
			num_len = 0;
			++i;
			break;
		case 'q':
			return lib_ok;
		case EOF:
			return lib_unexpected_eof;
		default:
			return lib_invalid_header;
		}
	}
	return (text_next_char(tp) == 'q') ? lib_ok : lib_invalid_header;
}

enum lib_fail sixel_calc_parameters(struct sixel_desc *desc) {
	/* Format (after DCS): P1 ; P2 ; P3 ; 'q'
	 * P1 is the pixel vertical aspect ratio, in range 0-9.
	 * P2 is whether 0 pixels are set to the background color or not
	 *     modified. In range 0-2.
	 * P3 is the horizontal grid size, the distance between two pixels.
	 *     I don't know its range.
	 * Any of these components may be omitted. */
	struct text_parser *tp = &desc->tp;

	unsigned char macro[3] = {0};
	enum lib_fail status = dcs_parse(tp, macro);
	if (status != lib_ok) {
		return status;
	}

	switch (macro[0]) {
	case 2:
		desc->pan = 5;
		break;
	case 3: case 4:
		desc->pan = 3;
		break;
	case 7: case 8: case 9:
		desc->pan = 1;
		break;
	case 0: case 1: case 5: case 6:
		desc->pan = 2;
		break;
	default:
		return lib_invalid_header;
	}
	desc->pad = 1;
	switch (macro[1]) {
	case 0: case 2:
		desc->p2 = sixel_set_to_bg;
		break;
	case 1:
		desc->p2 = sixel_retain;
		break;
	default:
		return lib_invalid_header;
	}
	desc->horizontal_grid_size = macro[2];

	desc->r = (struct raster_desc) {
		.ch = 4,
		.bitdepth = 8,
	};
	const int c = text_next_nonspace(tp);
	if (c == raster_attributes) {
		unsigned int raster[4] = {0};
		status = get_raster_attributes(tp, raster);
		if (status != lib_ok) {
			return status;
		} else if (raster[0] == 0 || raster[1] == 0) {
			return lib_invalid_header;
		}
		desc->pan = raster[0];
		desc->pad = raster[1];
		desc->r.w = raster[2];
		desc->r.h = raster[3];
	} else if (c == EOF) {
		return lib_unexpected_eof;
	} else {
		--tp->pos;
	}
	return calc_dimensions(desc);
}

static int skip_csi(struct text_parser *tp) {
	const int max_chars = 12;
	bool prev_escape = true;
	int c = 0;
	for (int i = 0; i < max_chars; ++i) {
		c = text_next_char(tp);
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

enum lib_fail sixel_open_mem(struct sixel_desc *desc,
const struct mmap_info *mem) {
	struct text_parser *tp = &desc->tp;
	*tp = text_parser_mem(mem->len, mem->data);

	/* The sixel format begins with the Device Control String, which might
	 * come in single-byte and two-byte form. And since it is basically a
	 * giant terminal command written to a file, some escape codes can be
	 * expected before that. */
	bool valid = false;
	int c = text_next_char(tp);
	if (c == ansi_escape) {
		c = skip_csi(tp);
		valid = (c == 'P');
	} else if (c == device_control_string) {
		valid = true;
	}

	if (valid) {
		return lib_ok;
	} else if (c == EOF) {
		return lib_unexpected_eof;
	}
	return lib_invalid_signature;
}
