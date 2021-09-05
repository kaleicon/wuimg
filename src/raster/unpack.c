#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include "../common.h"
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

void strip_spread(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t ch) {
	for (size_t x = 0; x < width; ++x) {
		dst[x*ch] = src[x];
	}
}

void strip_swizzle(uint8_t *dst, const uint8_t *src,
const struct raster_desc *restrict desc, const enum pix_layout target_layout) {
	uint8_t dst_swizzle[4];
	uint8_t src_swizzle[4];
	pix_swizzle_mask(dst_swizzle, target_layout);
	pix_swizzle_mask(src_swizzle, desc->layout);

	const size_t elem_size = desc->bitdepth / 8;
	const size_t padding = (desc->alignment - (desc->w * desc->ch * elem_size
		% desc->alignment)) % desc->alignment;

	for (size_t y = 0; y < desc->h; ++y) {
		for (size_t x = 0; x < desc->w; ++x) {
			uint8_t buf[8*4];
			for (uint8_t z = 0; z < desc->ch; ++z) {
				const size_t dst_z = dst_swizzle[z] * elem_size;
				const size_t src_z = src_swizzle[z] * elem_size;
				memcpy(buf + dst_z, src + src_z, elem_size);
			}
			const size_t span = elem_size * desc->ch;
			memcpy(dst, buf, span);
			dst += span;
			src += span;
		}
		src += padding;
	}
}
