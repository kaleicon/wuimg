#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

#include "common.h"
#include "common_unpack.h"

// Unpack n-bit onto unsigned chars
static void unpack4(const uint_fast8_t byte, u_int8_t *out, const size_t nr) {
	switch (nr) {
	case 2:
		out[1] = byte & 0x0f;
		// Fallthrough
	case 1:
		out[0] = (u_int8_t)(byte >> 4);
	}
}

static void unpack2(const uint_fast8_t byte, u_int8_t *out, const size_t nr) {
	switch (nr) {
	case 4:
		out[3] = byte & 0x03;
		// Fallthrough
	case 3:
		out[2] = (byte >> 2) & 0x03;
		// Fallthrough
	case 2:
		out[1] = (byte >> 4) & 0x03;
		// Fallthrough
	case 1:
		out[0] = (u_int8_t)(byte >> 6);
	}
}

static void unpack1(const uint_fast8_t byte, u_int8_t *out, const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		out[i] = (byte >> (7 - i)) & 0x01;
	}
}

// Expand n-bit to 8-bit
static void expand4(const uint_fast8_t byte, u_int8_t *out, const size_t nr) {
	const int s = 0xff / 0x0f;
	switch (nr) {
	case 2:
		out[1] = (u_int8_t)( (byte & 0x0f) * s );
		// Fallthrough
	case 1:
		out[0] = (u_int8_t)( (byte >> 4) * s);
	}
}

static void expand2(const uint_fast8_t byte, u_int8_t *out, const size_t nr) {
	const int_fast32_t s = 0xff / 0x03;
	switch (nr) {
	case 4:
		out[3] = (u_int8_t)( (byte & 0x03) * s );
		// Fallthrough
	case 3:
		out[2] = (u_int8_t)( ((byte >> 2) & 0x03) * s );
		// Fallthrough
	case 2:
		out[1] = (u_int8_t)( ((byte >> 4) & 0x03) * s );
		// Fallthrough
	case 1:
		out[0] = (u_int8_t)( (byte >> 6) * s );
		// Fallthrough
	}
}

static void expand_invert1(uint_fast8_t byte, u_int8_t *out, const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		out[i] = (byte & (0x80 >> i)) ? 0x00 : 0xff;
	}
}

static void expand1(const uint_fast8_t byte, u_int8_t *out, const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		out[i] = (byte & (0x80 >> i)) ? 0xff : 0x00;
	}
}

// Have the compiler inline one of the above
static inline void select_unpack(const uint_fast8_t byte, void *out,
const size_t nr, const enum unpack_op action, const size_t bitdepth) {
	switch (action) {
	case unpack:
		switch (bitdepth) {
		case 1: unpack1(byte, out, nr); break;
		case 2: unpack2(byte, out, nr); break;
		case 4: unpack4(byte, out, nr); break;
		}
		break;
	case expand:
		switch (bitdepth) {
		case 1: expand1(byte, out, nr); break;
		case 2: expand2(byte, out, nr); break;
		case 4: expand4(byte, out, nr); break;
		}
		break;
	case expand_invert:
		switch (bitdepth) {
		case 1: expand_invert1(byte, out, nr); break;
		case 2: expand2(byte ^ 0xff, out, nr); break;
		case 4: expand4(byte ^ 0xff, out, nr); break;
		}
		break;
	case noop:
	case pack:
	case pack_float:
		break;
	}
}

// Oddball out
void strip_invert8(u_int8_t *out, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		out[i] ^= 0xff;
	}
}

// The above but looping
static inline u_int8_t * strip_common(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const size_t boundary, const enum unpack_op op, const size_t bitdepth) {
	const size_t biab = 8 / bitdepth;

	const size_t remainer = width % biab;
	const size_t rowpad = scanline_length(width, bitdepth, boundary)
		- width / biab;
	const u_int8_t *restrict srcend = src + (width/biab) * height;
	while (src < srcend) {
		const u_int8_t *restrict rowend = src + (width/biab);
		while (src < rowend) {
			select_unpack(*src, out, biab, op, bitdepth);
			out += biab;
			++src;
		}
		select_unpack(*src, out, remainer, op, bitdepth);
		out += remainer;
		src += rowpad;
	}
	return out;
}

static u_int8_t * strip_invert4(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const size_t boundary) {
	return strip_common(out, src, width, height, boundary, expand_invert, 4);
}

static u_int8_t * strip_invert2(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const size_t boundary) {
	return strip_common(out, src, width, height, boundary, expand_invert, 2);
}

static u_int8_t * strip_invert1(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const size_t boundary) {
	return strip_common(out, src, width, height, boundary, expand_invert, 1);
}

static u_int8_t * strip_expand4(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const size_t boundary) {
	return strip_common(out, src, width, height, boundary, expand, 4);
}

static u_int8_t * strip_expand2(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const size_t boundary) {
	return strip_common(out, src, width, height, boundary, expand, 2);
}

static u_int8_t * strip_expand1(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const size_t boundary) {
	return strip_common(out, src, width, height, boundary, expand, 1);
}

static u_int8_t * strip_unpack4(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const size_t boundary) {
	return strip_common(out, src, width, height, boundary, unpack, 4);
}

static u_int8_t * strip_unpack2(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const size_t boundary) {
	return strip_common(out, src, width, height, boundary, unpack, 2);
}

static u_int8_t * strip_unpack1(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const size_t boundary) {
	return strip_common(out, src, width, height, boundary, unpack, 1);
}

static u_int8_t * strip_expand24(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t len) {
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
	return out;
}

static float * strip_pack64fp(float *out, const double *src, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		out[i] = (float)(src[i]);
	}
	return out + len;
}

static u_int32_t * strip_pack64(u_int32_t *out, const u_int64_t *src,
const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		out[i] = (u_int32_t)(src[i] >> 32);
	}
	return out + len;
}

void * strip_unpack(void *restrict out, const void *restrict src,
const size_t width, const size_t height, const size_t boundary,
const enum unpack_op op, const size_t bitdepth) {
	switch (op) {
	case noop: break;
	case unpack:
		switch (bitdepth) {
		case 1: return strip_unpack1(out, src, width, height, boundary);
		case 2: return strip_unpack2(out, src, width, height, boundary);
		case 4: return strip_unpack4(out, src, width, height, boundary);
		}
		break;
	case expand:
		switch (bitdepth) {
		case 1: return strip_expand1(out, src, width, height, boundary);
		case 2: return strip_expand2(out, src, width, height, boundary);
		case 4: return strip_expand4(out, src, width, height, boundary);
		case 24: return strip_expand24(out, src, width * height);
		}
		break;
	case expand_invert:
		switch (bitdepth) {
		case 1: return strip_invert1(out, src, width, height, boundary);
		case 2: return strip_invert2(out, src, width, height, boundary);
		case 4: return strip_invert4(out, src, width, height, boundary);
		}
		break;
	case pack:
		switch (bitdepth) {
		case 64: return strip_pack64(out, src, width * height);
		}
		break;
	case pack_float:
		switch (bitdepth) {
		case 64: return strip_pack64fp(out, src, width * height);
		}
		break;
	}
	return out;
}

// Now with color maps
static u_int8_t * expand_colormap(const uint_fast8_t byte,
u_int8_t *restrict out, const struct colormap *cm, const size_t nr,
const u_int8_t ch, const u_int8_t bitdepth, const bool careful) {
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

static inline u_int8_t * colormap_common(u_int8_t *restrict out,
const u_int8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary, const u_int8_t ch,
const u_int8_t bitdepth) {
	const size_t biab = 8UL / bitdepth;

	const size_t remainer = width % biab;
	const size_t rowpad = scanline_length(width, bitdepth, boundary)
		- width / biab;

	const bool is_rgb = (ch == 3);
	for (size_t i = 0; i < height - is_rgb; ++i) {
		for (size_t j = 0; j < width / biab; ++j) {
			out = expand_colormap(*src, out, cm, biab, ch, bitdepth,
				false);
			++src;
		}
		out = expand_colormap(*src, out, cm, remainer, ch, bitdepth, false);
		src += rowpad;
	}

	if (is_rgb) {
		for (size_t j = 0; j < width / biab; ++j) {
			out = expand_colormap(*src, out, cm, biab, ch, bitdepth,
				j == width / biab - 1);
			++src;
		}
		out = expand_colormap(*src, out, cm, remainer, ch, bitdepth, true);
	}
	return out;
}

static uint8_t * strip_colormap_rgba8(u_int8_t *restrict out,
const u_int8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary) {
	return colormap_common(out, src, cm, width, height, boundary, 4, 8);
}

static u_int8_t * strip_colormap_rgb8(u_int8_t *restrict out,
const u_int8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary) {
	return colormap_common(out, src, cm, width, height, boundary, 3, 8);
}

static u_int8_t * strip_colormap_rgb4(u_int8_t *restrict out,
const u_int8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary) {
	return colormap_common(out, src, cm, width, height, boundary, 3, 4);
}

static u_int8_t * strip_colormap_rgb2(u_int8_t *restrict out,
const u_int8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary) {
	return colormap_common(out, src, cm, width, height, boundary, 3, 2);
}

static u_int8_t * strip_colormap_rgb1(u_int8_t *restrict out,
const u_int8_t *restrict src, const struct colormap *cm, const size_t width,
const size_t height, const size_t boundary) {
	return colormap_common(out, src, cm, width, height, boundary, 3, 1);
}

void * strip_colormap(void *restrict out, const u_int8_t *restrict src,
const void *cm, const size_t width, const size_t height,
const u_int8_t boundary, const u_int8_t channels, const u_int8_t bitdepth) {
	switch (channels) {
	case 3:
		switch (bitdepth) {
		case 1: return strip_colormap_rgb1(out, src, cm, width, height, boundary);
		case 2: return strip_colormap_rgb2(out, src, cm, width, height, boundary);
		case 4: return strip_colormap_rgb4(out, src, cm, width, height, boundary);
		case 8: return strip_colormap_rgb8(out, src, cm, width, height, boundary);
		}
		break;
	case 4:
		switch (bitdepth) {
		case 8: return strip_colormap_rgba8(out, src, cm, width, height, boundary);
		}
		break;
	}
	return out;
}

// Special cases
static void expand_332(const int byte, u_int8_t *output) {
	const int rgscale = (UCHAR_MAX << 7) / 0x07 + 1;
	const int bscale = 0xff / 3;

	const int r = byte >> 5;
	const int g = (byte >> 2) & 0x07;
	const int b = byte & 0x03;

	output[0] = (unsigned char)((r * rgscale) >> 7);
	output[1] = (unsigned char)((g * rgscale) >> 7);
	output[2] = (unsigned char)(b * bscale);
}

u_int8_t * strip_expand332(u_int8_t *restrict out,
const u_int8_t *restrict src, const size_t width, const size_t height,
const unsigned char boundary) {
	const size_t rowpad = scanline_length(width, 8, boundary) - width;
	for (size_t i = 0; i < height; ++i) {
		for (size_t j = 0; j < width; ++j) {
			expand_332(*src, out);
			out += 3;
			++src;
		}
		src += rowpad;
	}
	return out;
}
