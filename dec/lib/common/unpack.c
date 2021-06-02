#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include "../../common.h"
#include "unpack.h"

// Unpack n-bits onto unsigned chars
static void unpack4(const uint_fast8_t byte, uint8_t *out, const size_t nr) {
	switch (nr) {
	case 2:
		out[1] = byte & 0x0f;
		// fallthrough
	case 1:
		out[0] = (uint8_t)(byte >> 4);
	}
}

static void unpack2(const uint_fast8_t byte, uint8_t *out, const size_t nr) {
	switch (nr) {
	case 4:
		out[3] = byte & 0x03;
		// fallthrough
	case 3:
		out[2] = (byte >> 2) & 0x03;
		// fallthrough
	case 2:
		out[1] = (byte >> 4) & 0x03;
		// fallthrough
	case 1:
		out[0] = (uint8_t)(byte >> 6);
	}
}

static void unpack1(const uint_fast8_t byte, uint8_t *out, const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		out[i] = (byte >> (7 - i)) & 0x01;
	}
}

// Expand n-bit to 8-bit
static void expand4(const uint_fast8_t byte, uint8_t *out, const size_t nr) {
/*
	const int s = 0xff / 0x0f;
	switch (nr) {
	case 2:
		out[1] = (uint8_t)( (byte & 0x0f) * s );
		// fallthrough
	case 1:
		out[0] = (uint8_t)( (byte >> 4) * s);
	}*/
	unpack4(byte, out, nr);
	for (size_t i = 0; i < nr; ++i) {
		out[i] *= (0xff / 0x0f);
	}
}

static void expand2(const uint_fast8_t byte, uint8_t *out, const size_t nr) {
	const int_fast32_t s = 0xff / 0x03;
	switch (nr) {
	case 4:
		out[3] = (uint8_t)( (byte & 0x03) * s );
		// fallthrough
	case 3:
		out[2] = (uint8_t)( ((byte >> 2) & 0x03) * s );
		// fallthrough
	case 2:
		out[1] = (uint8_t)( ((byte >> 4) & 0x03) * s );
		// fallthrough
	case 1:
		out[0] = (uint8_t)( (byte >> 6) * s );
		// fallthrough
	}
}

static void expand_invert1(uint_fast8_t byte, uint8_t *out, const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		out[i] = (byte & (0x80 >> i)) ? 0x00 : 0xff;
	}
}

static void expand1(const uint_fast8_t byte, uint8_t *out, const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		out[i] = (byte & (0x80 >> i)) ? 0xff : 0x00;
	}
}

// Have the compiler inline one of the above
static inline void select_unpack(const uint_fast8_t byte, uint8_t *out,
const size_t nr, const enum unpack_op action, const size_t bitdepth) {
	switch (action) {
	case op_unpack:
		switch (bitdepth) {
		case 1: unpack1(byte, out, nr); break;
		case 2: unpack2(byte, out, nr); break;
		case 4: unpack4(byte, out, nr); break;
		}
		break;
	case op_expand:
		switch (bitdepth) {
		case 1: expand1(byte, out, nr); break;
		case 2: expand2(byte, out, nr); break;
		case 4: expand4(byte, out, nr); break;
		}
		break;
	case op_expand_invert:
		switch (bitdepth) {
		case 1: expand_invert1(byte, out, nr); break;
		case 2: expand2(byte ^ 0xff, out, nr); break;
		case 4: expand4(byte ^ 0xff, out, nr); break;
		case 8: *out = byte ^ 0xff; break;
		}
		break;
	case op_noop:
	case op_pack:
	case op_pack_float:
		break;
	}
}

// The above but looping
static inline void strip_common(uint8_t *restrict out,
const uint8_t *restrict src, const size_t width, const size_t height,
const size_t boundary, const enum unpack_op op, const size_t bitdepth) {
	const size_t biab = 8 / bitdepth;

	const size_t bytes = width / biab;
	const size_t remainer = width % biab;
	const size_t line_end = width - remainer;
	const size_t scan = scanline_length(width, bitdepth, boundary);

	for (size_t y = 0; y < height; ++y) {
		const size_t src_y = y*scan;
		const size_t out_y = y*width;
		for (size_t x = 0; x < bytes; ++x) {
			const size_t i = src_y + x;
			const size_t o = out_y + x * biab;
			select_unpack(src[i], out + o, biab, op, bitdepth);
		}
		select_unpack(src[src_y + bytes], out + out_y + line_end,
			remainer, op, bitdepth);
	}
}

// Inline the common version in each variant for maximum speed
static void strip_invert8(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	// This one shouldn't exist.
	strip_common(out, src, width, height, boundary, op_expand_invert, 8);
}

static void strip_invert4(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(out, src, width, height, boundary, op_expand_invert, 4);
}

static void strip_invert2(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(out, src, width, height, boundary, op_expand_invert, 2);
}

static void strip_invert1(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(out, src, width, height, boundary, op_expand_invert, 1);
}

static void strip_expand4(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(out, src, width, height, boundary, op_expand, 4);
}

static void strip_expand2(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(out, src, width, height, boundary, op_expand, 2);
}

static void strip_expand1(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(out, src, width, height, boundary, op_expand, 1);
}

static void strip_unpack4(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(out, src, width, height, boundary, op_unpack, 4);
}

static void strip_unpack2(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(out, src, width, height, boundary, op_unpack, 2);
}

static void strip_unpack1(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(out, src, width, height, boundary, op_unpack, 1);
}

static void strip_expand24(uint8_t *restrict out,
const uint8_t *restrict src, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		if (which_end() == little_endian) {
			out[0] = src[2];
			out[1] = src[0];
			out[2] = src[1];
			out[3] = src[2];
		} else {
			out[0] = src[0];
			out[1] = src[1];
			out[2] = src[2];
			out[3] = src[0];
		}
		out += 4;
		src += 3;
	}
}

static void strip_pack64(uint32_t *out, const uint64_t *src,
const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		out[i] = (uint32_t)(src[i] >> 32);
	}
}

static void strip_pack64f(float *out, const double *src, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		out[i] = (float)(src[i]);
	}
}

void strip_unpack(void *restrict out, const void *restrict src,
const size_t width, const size_t height, const size_t boundary,
const enum unpack_op op, const size_t bitdepth) {
	switch (op) {
	case op_noop: break;
	case op_unpack:
		switch (bitdepth) {
		case 1: strip_unpack1(out, src, width, height, boundary); break;
		case 2: strip_unpack2(out, src, width, height, boundary); break;
		case 4: strip_unpack4(out, src, width, height, boundary); break;
		}
		break;
	case op_expand:
		switch (bitdepth) {
		case 1: strip_expand1(out, src, width, height, boundary); break;
		case 2: strip_expand2(out, src, width, height, boundary); break;
		case 4: strip_expand4(out, src, width, height, boundary); break;
		case 24: strip_expand24(out, src, width * height); break;
		}
		break;
	case op_expand_invert:
		switch (bitdepth) {
		case 1: strip_invert1(out, src, width, height, boundary); break;
		case 2: strip_invert2(out, src, width, height, boundary); break;
		case 4: strip_invert4(out, src, width, height, boundary); break;
		case 8: strip_invert8(out, src, width, height, boundary); break;
		}
		break;
	case op_pack:
		switch (bitdepth) {
		case 64: strip_pack64(out, src, width * height); break;
		}
		break;
	case op_pack_float:
		switch (bitdepth) {
		case 64: strip_pack64f(out, src, width * height); break;
		}
		break;
	}
}

// Now with color maps
static uint8_t * expand_colormap(const uint_fast8_t byte,
uint8_t *restrict out, const struct colormap *cm, const size_t nr,
const size_t ch, const size_t bitdepth, const bool careful) {
	const int mask = (1 << bitdepth) - 1;
	size_t i = 8;
	const size_t bound = i - nr * bitdepth;
	while (i > bound) {
		i -= bitdepth;
		const int idx = (byte >> i) & mask;
		memcpy(out, cm + idx, careful ? 3 : 4);
		out += ch;
	}
	return out;
}

static inline void colormap_common(uint8_t *restrict out,
const uint8_t *restrict src, const struct colormap *cm, size_t width,
const size_t height, const size_t boundary, const size_t ch,
const size_t bitdepth) {
	const size_t biab = 8 / bitdepth;

	const size_t bytes = width / biab;
	const size_t remainer = width % biab;
	const size_t scan = scanline_length(width, bitdepth, boundary);
	const size_t rowpad = scan - width / biab;

	const bool is_rgb = (ch == 3);
	for (size_t y = 0; y < height - is_rgb; ++y) {
		for (size_t x = 0; x < bytes; ++x) {
			out = expand_colormap(*src, out, cm, biab, ch, bitdepth,
				false);
			++src;
		}
		out = expand_colormap(*src, out, cm, remainer, ch, bitdepth, false);
		src += rowpad;
	}

	if (is_rgb) {
		for (size_t x = 0; x < bytes; ++x) {
			out = expand_colormap(*src, out, cm, biab, ch, bitdepth,
				x == bytes - 1);
			++src;
		}
		out = expand_colormap(*src, out, cm, remainer, ch, bitdepth, true);
	}
}

static void strip_colormap_rgba8(uint8_t *restrict out,
const uint8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary) {
	colormap_common(out, src, cm, width, height, boundary, 4, 8);
}

static void strip_colormap_rgb8(uint8_t *restrict out,
const uint8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary) {
	colormap_common(out, src, cm, width, height, boundary, 3, 8);
}

static void strip_colormap_rgb4(uint8_t *restrict out,
const uint8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary) {
	colormap_common(out, src, cm, width, height, boundary, 3, 4);
}

static void strip_colormap_rgb2(uint8_t *restrict out,
const uint8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary) {
	colormap_common(out, src, cm, width, height, boundary, 3, 2);
}

static void strip_colormap_rgb1(uint8_t *restrict out,
const uint8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary) {
	colormap_common(out, src, cm, width, height, boundary, 3, 1);
}

void strip_colormap(void *restrict out, const uint8_t *restrict src,
const void *cm, const size_t width, const size_t height,
const size_t boundary, const size_t channels, const size_t bitdepth) {
	switch (channels) {
	case 3:
		switch (bitdepth) {
		case 1:
			strip_colormap_rgb1(out, src, cm, width, height, boundary);
			break;
		case 2:
			strip_colormap_rgb2(out, src, cm, width, height, boundary);
			break;
		case 4:
			strip_colormap_rgb4(out, src, cm, width, height, boundary);
			break;
		case 8:
			strip_colormap_rgb8(out, src, cm, width, height, boundary);
			break;
		}
		break;
	case 4:
		switch (bitdepth) {
		case 8:
			strip_colormap_rgba8(out, src, cm, width, height, boundary);
			break;
		}
		break;
	}
}

// Special cases
static void expand332(const int byte, uint8_t *output) {
	const int rgscale = (UCHAR_MAX << 7) / 0x07 + 1;
	const int bscale = 0xff / 3;

	const int r = byte >> 5;
	const int g = (byte >> 2) & 0x07;
	const int b = byte & 0x03;

	output[0] = (unsigned char)((r * rgscale) >> 7);
	output[1] = (unsigned char)((g * rgscale) >> 7);
	output[2] = (unsigned char)(b * bscale);
}

void strip_expand332(uint8_t *restrict out, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	const size_t rowpad = scanline_length(width, 8, boundary) - width;
	for (size_t i = 0; i < height; ++i) {
		for (size_t j = 0; j < width; ++j) {
			expand332(*src, out);
			out += 3;
			++src;
		}
		src += rowpad;
	}
}

void pixel_expand555(void *restrict out, const uint16_t word) {
	const uint_fast32_t p = 6;
	const uint_fast32_t scale = (0xff << p) / 0x1f + 1;

	const uint_fast32_t w = word; // Fix conversion warning
	uint8_t *o = out;
	for (uint_fast32_t n = 0; n < 3; ++n) {
		const uint_fast32_t m = n*5;
		o[n] = (uint8_t)( ((w & (0x1f<<m)) * scale) >> (p+m) );
//		o[n] = (uint8_t)( ((word & (uint_fast32_t)(0x1f<<m)) * scale)
//			>> (p+m) );
	}
}

static inline void strip_interleave_inline(uint8_t *restrict out,
const uint8_t *restrict src, const size_t dims, const size_t planes,
const size_t word) {
	for (size_t x = 0; x < dims; ++x) {
		for (size_t p = 0; p < planes; ++p) {
			const size_t dst_off = (x*planes + p) * word;
			const size_t src_off = (dims*p + x) * word;
			memcpy(out + dst_off, src + src_off, word);
		}
	}
}

static void strip_interleave8(uint8_t *restrict out,
const uint8_t *restrict src, const size_t dims, const size_t planes) {
	const size_t word = 1;
	switch (planes) {
	case 2: strip_interleave_inline(out, src, dims, planes, word); break;
	case 3: strip_interleave_inline(out, src, dims, planes, word); break;
	case 4: strip_interleave_inline(out, src, dims, planes, word); break;
	}
}

void strip_interleave(void *restrict out, const void *restrict src,
const size_t dims, const size_t planes, const size_t bitdepth) {
	const size_t word = (size_t)bitdepth / 8;
	// Currently, the mess that is PCX depends on this.
	if (planes == 1) {
		puts("An unnecesary copy has been made. Repent.");
		memcpy(out, src, dims * word);
		return;
	}

	switch (bitdepth) {
	case 8: strip_interleave8(out, src, dims, planes); break;
	default:
		strip_interleave_inline(out, src, dims, planes, word);
	}
}

// Utils
void * strip_map_colormap(FILE *ifp, const struct colormap *cm,
const size_t width, const size_t height, const size_t boundary,
const size_t channels, const size_t bitdepth) {
	const size_t out_size = width * channels * height;
	unsigned char *output = NULL;
	if (channels == 3 || channels == 4) {
		output = malloc(out_size);
		if (output) {
			const size_t data_size = scanline_length(width,
				boundary, bitdepth) * height;
			void *bits = output + out_size - data_size;
			fread(bits, 1, data_size, ifp);
			strip_colormap(output, bits, cm, width, height,
				boundary, channels, bitdepth);
		}
	}
	return output;
}

void * strip_map_unpack(FILE *ifp, const size_t width, const size_t height,
const size_t boundary, const enum unpack_op op, const size_t bitdepth) {
	const size_t data_size = scanline_length(width, boundary, bitdepth)
		* height;
	unsigned char *output = NULL;
	switch (op) {
	case op_noop:
		output = malloc(data_size);
		if (output) {
			fread(output, 1, data_size, ifp);
		}
		break;
	case op_unpack:
	case op_expand:
	case op_expand_invert:
		;
		const size_t dims = width * height;
		output = malloc(dims);
		if (output) {
			void *bits = output + dims - data_size;
			fread(bits, 1, data_size, ifp);
			strip_unpack(output, bits, width, height, boundary,
				op, bitdepth);
		}
		break;
	case op_pack:
	case op_pack_float:
		break;
	}
	return output;
}
/*
enum lib_fail strip_file(struct lib_raster *raster, FILE *ifp,
const bool expand_raster) {
	const enum unpack_op op = expand_raster ? op_expand
	raster->data = strip_map_unpack(ifp, raster->w * raster->components,
		raster->h, raster->align, op, raster->depth);
	return raster->data ? lib_ok : lib_alloc_error;
}*/
