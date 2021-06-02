#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include "common/lib.h"
#include "common/text.h"
#include "../../common.h"
#include "sixel.h"

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


void sixel_cleanup(struct sixel_desc *desc) {
	free(desc->data);
}

static bool issixel(const unsigned char c) {
	return c >= '?' && c <= '~';
}

static size_t read_3digits(const unsigned char *restrict data,
const size_t len, int_fast16_t *val) {
	const size_t iters = zumin(len, 3);
	size_t i = 0;
	while (i < iters) {
		const int_fast16_t d = tonum(data[i]);
		if (d == -1) {
			break;
		}
		*val = *val * 10 + d;
		++i;
	}
	return i;
}

static unsigned char hls_to_rgb(const int_fast16_t n,
const int_fast16_t comp[3], const int_fast16_t mult) {
	/* From
	 * https://en.wikipedia.org/wiki/HLS_color_space#HSL_to_RGB_alternative
	 * of course. */
	const int_fast16_t h = comp[0];
	const int_fast16_t l = comp[1];
	const int_fast16_t s = comp[2];

	const int_fast16_t k = (n + h / 30) % (12 * mult);
	const int_fast16_t a = s * lmin(l, mult - l) / mult;
	const int_fast16_t min = lmin( lmin(k - 3*mult, 9*mult - k), mult);
	const int_fast16_t max = lmax(-mult, min);

	const int_fast16_t result = l - a * max / mult;
	return (unsigned char)((result * UCHAR_MAX) / mult);
}

static void normalize_color(void *restrict map, int_fast16_t comp[3],
enum sixel_colorspace pu) {
	unsigned char *restrict rgba = map;

	const int_fast16_t mult = 1 << 8;
	int_fast16_t scale;
	switch (pu) {
	case sixel_hls:
		// Gotta go fast, no time for floats
		scale = (0x100 * mult) / 100 + 1;
		comp[0] *= mult;
		comp[1] = (comp[1] * scale) / mult;
		comp[2] = (comp[2] * scale) / mult;
		for (int_fast16_t i = 0; i < 3; ++i) {
			const int_fast16_t n = ((12 - i*4) % 12) * mult;
			rgba[i] = hls_to_rgb(n, comp, mult);
		}
		break;
	case sixel_rgb:
		scale = (UCHAR_MAX * mult) / 100 + 1;
		for (size_t i = 0; i < 3; ++i) {
			rgba[i] = (unsigned char)((comp[i] * scale) / mult);
		}
		break;
	}
}

static size_t read_color(const unsigned char *restrict data, const size_t len,
struct sixel_colormap *map) {
	/* Format:
	 * (select) # Pc
	 * (set)    # Pc ; Pu ; Px ; Py ; Pz
	 * Pc is the color index, in range 0-255.
	 * Pu is the color space, either 1 (HLS) or 2 (RGB). Required.
	 * Px is the first component
	 *    in range 0-360 if HLS.
	 *    in range 0-100 if RGB.
	 * Py and Pz are the second and third components, in range 0-100. */
	int_fast16_t idx = 0;
	size_t pos = read_3digits(data, len, &idx);
	if (idx > UCHAR_MAX) {
		return 0;
	} else if (pos >= len || data[pos] != ';') { // select form
		if (map) {
			map->active = (size_t)idx;
		}
	} else {
		if (pos + 4 >= len) {
			return 0;
		}
		++pos;

		enum sixel_colorspace pu;
		switch (data[pos]) {
		case sixel_hls:
		case sixel_rgb:
			pu = data[pos];
			break;
		default:
			return 0;
		}
		++pos;

		int_fast16_t tmp[3] = {0};
		for (size_t i = 0; i < ARRAY_LEN(tmp); ++i) {
			if (pos >= len || data[pos] != ';') {
				return 0;
			}
			++pos;
			pos += read_3digits(data + pos, len - pos, tmp + i);
		}

		if (map) {
			normalize_color(map->map + idx, tmp, pu);
		} else { // Validation stage
			for (size_t i = 0; i < ARRAY_LEN(tmp); ++i) {
				int_fast16_t max;
				if (i == 0 && pu == sixel_hls) {
					max = 360;
				} else {
					max = 100;
				}
				if (tmp[i] > max) {
					return 0;
				}
			}
		}
	}
	return pos;
}

static void write_color(uint32_t *out, const struct sixel_colormap *map,
const size_t stride, uint_fast16_t sixel, const size_t len) {
	sixel -= '?';
	const uint32_t *color = (uint32_t *)(map->map + map->active);
	for (size_t i = 0; i < 6; ++i) {
		if ((sixel >> i) & 0x01) {
			for (size_t j = 0; j < len; ++j) {
				out[stride*i + j] = *color;
			}
		}
	}
}

static void default_colormap(struct sixel_colormap *map) {
	const struct colormap xterm[] = {
		{0,   0,   0,   255},
		{205, 0,   0,   255},
		{0,   205, 0,   255},
		{205, 205, 0,   255},
		{0,   0,   238, 255},
		{205, 0,   205, 255},
		{0,   205, 205, 255},
		{229, 229, 229, 255},

		{127, 127, 127, 255},
		{255, 0,   0,   255},
		{0,   255, 0,   255},
		{255, 255, 0,   255},
		{92,   92, 255, 255},
		{255, 0,   255, 255},
		{0,   255, 255, 255},
		{255, 255, 255, 255},
	};
	struct colormap *pal = map->map;
	memcpy(pal, xterm, sizeof(xterm));

	const unsigned char cube[] = {0, 0x5f, 0x87, 0xaf, 0xd7, 0xff};
	size_t pos = ARRAY_LEN(xterm);
	for (size_t r = 0; r < 6; ++r) {
		for (size_t g = 0; g < 6; ++g) {
			for (size_t b = 0; b < 6; ++b) {
				pal[pos].r = cube[r];
				pal[pos].g = cube[g];
				pal[pos].b = cube[b];
				pal[pos].a = 0xff;
				++pos;
			}
		}
	}

	unsigned char gray = 0x08;
	while (pos < ARRAY_LEN(map->map)) {
		pal[pos].r = gray;
		pal[pos].g = gray;
		pal[pos].b = gray;
		pal[pos].a = 0xff;
		gray = (unsigned char)(gray + 0x0a);
		++pos;
	}
}

uint32_t * sixel_decode(struct sixel_desc *desc) {
	const size_t dims = desc->w * desc->h;
	uint32_t *out = calloc(dims, 4);
	if (!out) {
		return NULL;
	}

	struct sixel_colormap map = {0};
	default_colormap(&map);
	size_t x = 0;
	size_t y = 0;
	// We've already validated the data so we can omit all checks.
	for (size_t i = 0;;) {
		unsigned char c = desc->data[i];
		++i;
		switch (c) {
		case ansi_escape:
			return out;
		case graphics_new_line:
			y += 6;
			// fallthrough
		case graphics_carriage_return:
			x = 0;
			break;
		case graphics_repeat_introducer:
			;int_fast16_t repeat = 0;
			i += read_3digits(desc->data + i, desc->data_len - i,
				&repeat);
			c = desc->data[i];
			++i;

			const size_t pos = y*desc->w;
			const size_t pixs = (size_t)repeat;
			write_color(out + pos + x, &map, desc->w, c, pixs);
			x += pixs;
			break;
		case color_introducer:
			i += read_color(desc->data + i, desc->data_len - i,
				&map);
			break;
		default:
			if (issixel(c)) {
				const size_t pos = y*desc->w;
				write_color(out + pos + x, &map, desc->w, c, 1);
				++x;
			}
		}
	}
	return out;
}

static enum lib_fail calc_dimensions(struct sixel_desc *desc) {
	/* We must do a pass over the whole stream to know the image
	 * dimensions. No other way around it. */
	const size_t len = (size_t)file_get_remaining(desc->ifp);
	desc->data = malloc(len + 1);
	if (!desc->data) {
		return lib_alloc_error;
	}
	fread(desc->data, 1, len, desc->ifp);

	size_t row_width = 0;
	size_t height = 6;
	bool found_end = false;

	size_t i = 0;
	while (i < len && !found_end) {
		const unsigned char c = desc->data[i];
		++i;

		size_t read;
		switch (c) {
		case ansi_escape:
			found_end = true;
			break;
		case graphics_new_line:
			height += 6;
			// fallthrough
		case graphics_carriage_return:
			if (row_width > desc->w) {
				desc->w = row_width;
			}
			row_width = 0;
			break;
		case graphics_repeat_introducer:
			;int_fast16_t repeat = 0;
			read = read_3digits(desc->data + i, len - i, &repeat);
			if (!read) {
				puts("SIXEL Error: Repeat introducer lacks "
					"digits.");
				return lib_invalid_data;
			}

			i += read;
			if (i >= len || !issixel(desc->data[i])) {
				puts("SIXEL Error: Found non-sixel graphics "
					"repeat.");
				return lib_invalid_data;
			}
			++i;
			row_width += (size_t)repeat;
			break;
		case color_introducer:
			read = read_color(desc->data + i, len - i, NULL);
			if (!read) {
				puts("SIXEL Error: Failed to parse color "
					"introducer.");
				return lib_invalid_data;
			}
			i += read;
			break;
		default:
			if (issixel(c)) {
				++row_width;
				continue;
			} else if (c >= 0x80) {
				found_end = true;
			} else if (!isspace(c)) {
				printf("SIXEL Error: Found invalid character.");
				return lib_invalid_data;
			}
		}
	}

	if (row_width > desc->w) {
		desc->w = row_width;
	}
	if (height > desc->h) {
		desc->h = height;
	}
	if (i <= (size_t)found_end || !desc->w) {
		return lib_invalid_data;
	}
	if (found_end) {
		--i;
	}
	desc->data[i] = ansi_escape;
	desc->data_len = i;
	return lib_ok;
}

static enum lib_fail get_raster_attributes(int raster[4], FILE *ifp) {
	// Format: " Pan ; Pad ; Ph ; Pv
	// Pan and Pad are required
	for (size_t i = 0; i < 4;) {
		int c = getc(ifp);
		switch (c) {
		case EOF: return lib_unexpected_eof;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			raster[i] = raster[i] * 10 + c - '0';
			break;
		case ';':
			++i;
			break;
		default:
			if (i < 2) {
				return lib_invalid_header;
			}
			ungetc(c, ifp);
			return lib_ok;
		}
	}
	// Can't have more than three colons
	return lib_invalid_header;
}

static enum lib_fail macro_parse(unsigned char macro[3], FILE *ifp) {
	/* Format: DCS P1 ; P2 ; P3 ; 'q'
	 * where P1 is in range 0-9, P2 in 0-2, and P3 i don't know */
	int num_len = 0;
	for (size_t i = 0; i < 3;) {
		int c = getc(ifp);
		switch (c) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (num_len) {
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
	if (getc(ifp) == 'q') {
		return lib_ok;
	}
	return lib_invalid_header;
}

enum lib_fail sixel_calc_parameters(struct sixel_desc *desc) {
	unsigned char macro[3] = {0};
	enum lib_fail status = macro_parse(macro, desc->ifp);
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
	default:
		desc->pan = 2;
	}
	desc->pad = 1;
	switch (macro[1]) {
	case 0: case 2:
		desc->p2 = set_to_bg;
		break;
	case 1:
		desc->p2 = retain;
		break;
	default:
		return lib_invalid_header;
	}
	desc->horizontal_grid_size = macro[2];

	int c;
	do {
		c = getc(desc->ifp);
	} while (isspace(c));

	if (c == raster_attributes) {
		int raster[4] = {0};
		status = get_raster_attributes(raster, desc->ifp);
		if (status != lib_ok) {
			return status;
		} else if (raster[0] == 0 || raster[1] == 0) {
			return lib_invalid_header;
		}
		desc->pan = raster[0];
		desc->pad = raster[1];
		desc->w = (size_t)raster[2];
		desc->h = (size_t)raster[3];
	} else if (c == EOF) {
		return lib_unexpected_eof;
	} else {
		ungetc(c, desc->ifp);
	}
	return calc_dimensions(desc);
}

static int skip_csi(FILE *ifp) {
	const int max_chars = 12;
	bool escape = true;
	int c;
	for (int i = 0; i < max_chars; ++i) {
		c = getc(ifp);
		if (escape) {
			if (c == 'P') {
				return c;
			}
			escape = false;
		} else if (c == ansi_escape) {
			escape = true;
		}
	}
	return c;
}

enum lib_fail sixel_open_file(FILE *ifp, struct sixel_desc *desc) {
	memset(desc, 0, sizeof(*desc));

	int c = getc(ifp);
	bool valid = false;
	if (c == ansi_escape) {
		c = skip_csi(ifp);
		if (c == 'P') {
			valid = true;
		}
	} else if (c == device_control_string) {
		valid = true;
	}

	if (valid) {
		desc->ifp = ifp;
		return lib_ok;
	} else if (c == EOF) {
		return lib_unexpected_eof;
	}
	return lib_invalid_signature;
}
