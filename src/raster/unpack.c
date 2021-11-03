#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "../common.h"
#include "unpack.h"

// Unpack n-bits onto unsigned chars
static void unpack4(const uint_fast8_t byte, uint8_t *dst, const size_t nr) {
	switch (nr) {
	case 2:
		dst[1] = byte & 0x0f;
		// fallthrough
	case 1:
		dst[0] = (uint8_t)(byte >> 4);
	}
}

static void unpack2(const uint_fast8_t byte, uint8_t *dst, const size_t nr) {
	switch (nr) {
	case 4:
		dst[3] = byte & 0x03;
		// fallthrough
	case 3:
		dst[2] = (byte >> 2) & 0x03;
		// fallthrough
	case 2:
		dst[1] = (byte >> 4) & 0x03;
		// fallthrough
	case 1:
		dst[0] = (uint8_t)(byte >> 6);
	}
}

static void unpack1(const uint_fast8_t byte, uint8_t *dst, const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		dst[i] = (byte >> (7 - i)) & 0x01;
	}
}

// Expand n-bit to 8-bit
static void expand4(const uint_fast8_t byte, uint8_t *dst, const size_t nr) {
/*
	const int s = 0xff / 0x0f;
	switch (nr) {
	case 2:
		dst[1] = (uint8_t)( (byte & 0x0f) * s );
		// fallthrough
	case 1:
		dst[0] = (uint8_t)( (byte >> 4) * s);
	}*/
	unpack4(byte, dst, nr);
	for (size_t i = 0; i < nr; ++i) {
		dst[i] *= (0xff / 0x0f);
	}
}

static void expand2(const uint_fast8_t byte, uint8_t *dst, const size_t nr) {
	const int_fast32_t s = 0xff / 0x03;
	switch (nr) {
	case 4:
		dst[3] = (uint8_t)( (byte & 0x03) * s );
		// fallthrough
	case 3:
		dst[2] = (uint8_t)( ((byte >> 2) & 0x03) * s );
		// fallthrough
	case 2:
		dst[1] = (uint8_t)( ((byte >> 4) & 0x03) * s );
		// fallthrough
	case 1:
		dst[0] = (uint8_t)( (byte >> 6) * s );
		// fallthrough
	}
}

static void expand1(const uint_fast8_t byte, uint8_t *dst, const size_t nr) {
	for (size_t i = 0; i < nr; ++i) {
		dst[i] = (byte & (0x80 >> i)) ? 0xff : 0x00;
	}
}

// Have the compiler inline one of the above
static inline void select_unpack(const uint_fast8_t byte, uint8_t *dst,
const size_t nr, const enum unpack_op action, const size_t bitdepth) {
	switch (action) {
	case op_unpack:
		switch (bitdepth) {
		case 1: unpack1(byte, dst, nr); break;
		case 2: unpack2(byte, dst, nr); break;
		case 4: unpack4(byte, dst, nr); break;
		}
		break;
	case op_expand:
		switch (bitdepth) {
		case 1: expand1(byte, dst, nr); break;
		case 2: expand2(byte, dst, nr); break;
		case 4: expand4(byte, dst, nr); break;
		case 8: *dst = byte; break;
		}
		break;
	case op_noop:
	case op_pack:
		break;
	}
}

// The above but looping
static inline void strip_common(uint8_t *restrict dst,
const uint8_t *restrict src, const size_t width, const size_t height,
const size_t boundary, const enum unpack_op op, const size_t bitdepth,
const enum pix_attr attr) {
	const size_t biab = 8 / bitdepth;

	const size_t bytes = width / biab;
	const size_t remainer = width % biab;
	const size_t line_end = width - remainer;
	const size_t scan = scanline_length(width, bitdepth, boundary);

	const uint8_t invert = (attr == pix_inverted) ? 0xff : 0x00;
	for (size_t y = 0; y < height; ++y) {
		const size_t src_y = y*scan;
		const size_t dst_y = y*width;
		for (size_t x = 0; x < bytes; ++x) {
			const size_t o = dst_y + x * biab;
			uint8_t byte = src[src_y + x] ^ invert;
			select_unpack(byte, dst + o, biab, op, bitdepth);
		}
		uint8_t byte = src[src_y + bytes] ^ invert;
		select_unpack(byte, dst + dst_y + line_end, remainer, op,
			bitdepth);
	}
}

// Inline the common version in each variant for maximum speed
static void strip_invert8(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(dst, src, width, height, boundary, op_expand, 8, pix_inverted);
}

static void strip_invert4(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(dst, src, width, height, boundary, op_expand, 4, pix_inverted);
}

static void strip_invert2(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(dst, src, width, height, boundary, op_expand, 2, pix_inverted);
}

static void strip_invert1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(dst, src, width, height, boundary, op_expand, 1, pix_inverted);
}

static void strip_expand4(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(dst, src, width, height, boundary, op_expand, 4, pix_normal);
}

static void strip_expand2(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(dst, src, width, height, boundary, op_expand, 2, pix_normal);
}

static void strip_expand1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(dst, src, width, height, boundary, op_expand, 1, pix_normal);
}

static void strip_unpack4(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(dst, src, width, height, boundary, op_unpack, 4, pix_normal);
}

static void strip_unpack2(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(dst, src, width, height, boundary, op_unpack, 2, pix_normal);
}

static void strip_unpack1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	strip_common(dst, src, width, height, boundary, op_unpack, 1, pix_normal);
}

static void strip_expand24(uint8_t *restrict dst,
const uint8_t *restrict src, const size_t len) {
	// src should be in native order
	for (size_t i = 0; i < len; ++i) {
		if (which_end() == little_endian) {
			dst[0] = src[2];
			dst[1] = src[0];
			dst[2] = src[1];
			dst[3] = src[2];
		} else {
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = src[0];
		}
		dst += 4;
		src += 3;
	}
}

static void strip_pack24_16(uint16_t *dst, const uint8_t *src,
const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		int word;
		if (which_end() == little_endian) {
			word = (src[i*3 + 2] << 16) | (src[i*3 + 1]);
		} else {
			word = (src[i*3] << 16) | src[i*3 + 1];
		}
		dst[i] = (uint16_t)word;
	}
}

static void strip_pack32_16(uint16_t *dst, const uint32_t *src,
const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		dst[i] = (uint16_t)(src[i] >> (32 - 16));
	}
}

static void strip_pack64_16(uint16_t *dst, const uint64_t *src,
const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		dst[i] = (uint16_t)(src[i] >> (64 - 16));
	}
}

static void strip_pack64f_32f(float *dst, const double *src, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		dst[i] = (float)(src[i]);
	}
}

// Special cases
static void expand1555(const uint16_t word, uint8_t *dst) {
	const uint8_t mask = (1 << 5) - 1;
	const uint8_t p = 6;
	const uint16_t scale = (UCHAR_MAX << p) / mask + 1;

	for (int n = 0; n < 3; ++n) {
		const int m = n*5;
		dst[n] = (uint8_t)( (scale * (word & (mask << m))) >> (p+m) );
	}
	dst[3] = (word >> 15) ? 0xff : 0x00;
}

static void strip_expand1555(uint8_t *restrict dst,
const uint16_t *restrict src, const size_t width, const size_t height,
const size_t boundary) {
	const size_t rowpad = scanline_length(width, 16, boundary)/2 - width;
	for (size_t y = 0; y < height; ++y) {
		for (size_t x = 0; x < width; ++x) {
			expand1555(*src, dst);
			dst += 4;
			++src;
		}
		src += rowpad;
	}
}

static void expand332(const int byte, uint8_t *dst) {
	const int rgscale = (UCHAR_MAX << 7) / 0x07 + 1;
	const int bscale = 0xff / 3;

	const int r = byte >> 5;
	const int g = (byte >> 2) & 0x07;
	const int b = byte & 0x03;

	dst[0] = (unsigned char)((r * rgscale) >> 7);
	dst[1] = (unsigned char)((g * rgscale) >> 7);
	dst[2] = (unsigned char)(b * bscale);
}

static void strip_expand332(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t height, const size_t boundary) {
	const size_t rowpad = scanline_length(width, 8, boundary) - width;
	for (size_t i = 0; i < height; ++i) {
		for (size_t j = 0; j < width; ++j) {
			expand332(*src, dst);
			dst += 3;
			++src;
		}
		src += rowpad;
	}
}

void unpack_strip(void *restrict dst, const void *restrict src,
const size_t width, const size_t height, const size_t alignment,
const enum pix_attr attr, const enum unpack_op op, const size_t bitdepth) {
	switch (attr) {
	case pix_normal:
		switch (op) {
		case op_noop: break;
		case op_unpack:
			switch (bitdepth) {
			case 1: strip_unpack1(dst, src, width, height, alignment); break;
			case 2: strip_unpack2(dst, src, width, height, alignment); break;
			case 4: strip_unpack4(dst, src, width, height, alignment); break;
			}
			break;
		case op_expand:
			switch (bitdepth) {
			case 1: strip_expand1(dst, src, width, height, alignment); break;
			case 2: strip_expand2(dst, src, width, height, alignment); break;
			case 4: strip_expand4(dst, src, width, height, alignment); break;
			case 24: strip_expand24(dst, src, width * height); break;
			}
			break;
		case op_pack:
			switch (bitdepth) {
			case 24: strip_pack24_16(dst, src, width * height); break;
			case 32: strip_pack32_16(dst, src, width * height); break;
			case 64: strip_pack64_16(dst, src, width * height); break;
			}
			break;
		}
		break;
	case pix_inverted:
		if (op == op_expand) {
			switch (bitdepth) {
			case 1: strip_invert1(dst, src, width, height, alignment); break;
			case 2: strip_invert2(dst, src, width, height, alignment); break;
			case 4: strip_invert4(dst, src, width, height, alignment); break;
			case 8: strip_invert8(dst, src, width, height, alignment); break;
			}
		}
		break;
	case pix_float:
		if (op == op_pack && bitdepth == 64) {
			strip_pack64f_32f(dst, src, width * height);
		}
		break;
	case pix_packing_332:
		if (op == op_expand && bitdepth == 8) {
			strip_expand332(dst, src, width, height, alignment);
		}
		break;
	case pix_packing_1555:
		if (op == op_expand && bitdepth == 16) {
			strip_expand1555(dst, src, width, height, alignment);
		}
		break;
	}
}

uint8_t unpack_depth(const enum pix_attr attr, const enum unpack_op op,
const size_t bitdepth) {
	uint8_t outdepth = 0;
	switch (attr) {
	case pix_normal:
		switch (op) {
		case op_noop: break;
		case op_expand:
			if (bitdepth == 24) {
				outdepth = 32;
				break;
			}
			// Fallthrough
		case op_unpack:
			switch (bitdepth) {
			case 1: case 2: case 4:
				outdepth = 8;
				break;
			}
			break;
		case op_pack:
			switch (bitdepth) {
			case 24: case 32: case 64:
				outdepth = 16;
			}
			break;
		}
		break;
	case pix_inverted:
		if (op == op_expand) {
			switch (bitdepth) {
			case 1: case 2: case 4: case 8:
				outdepth = 8;
				break;
			}
		}
		break;
	case pix_float:
		if (op == op_pack && bitdepth == 64) {
			outdepth = 32;
		}
		break;
	case pix_packing_332:
		if (op == op_expand && bitdepth == 8) {
			outdepth = 24;
		}
		break;
	case pix_packing_1555:
		if (op == op_expand && bitdepth == 16) {
			outdepth = 32;
		}
		break;
	}

	return outdepth;
}

size_t unpack_stride(const size_t width, enum pix_attr attr,
const enum unpack_op op, const size_t bitdepth) {
	uint8_t outdepth = unpack_depth(attr, op, bitdepth);
	if (outdepth) {
		return scanline_length(width, outdepth, 1);
	}
	return 0;
}

static inline void strip_interleave_inline(uint8_t *restrict dst,
const uint8_t *restrict src, const size_t dims, const size_t planes,
const size_t word) {
	for (size_t x = 0; x < dims; ++x) {
		for (size_t p = 0; p < planes; ++p) {
			const size_t dst_off = (x*planes + p) * word;
			const size_t src_off = (dims*p + x) * word;
			memcpy(dst + dst_off, src + src_off, word);
		}
	}
}

static void strip_interleave8(uint8_t *restrict dst,
const uint8_t *restrict src, const size_t dims, const size_t planes) {
	const size_t word = 1;
	switch (planes) {
	case 2: strip_interleave_inline(dst, src, dims, planes, word); break;
	case 3: strip_interleave_inline(dst, src, dims, planes, word); break;
	case 4: strip_interleave_inline(dst, src, dims, planes, word); break;
	}
}

void strip_interleave(void *restrict dst, const void *restrict src,
const size_t dims, const size_t planes, const size_t bitdepth) {
	const size_t word = (size_t)bitdepth / 8;
	if (planes == 1) {
		puts("An unnecesary copy has been made. Repent.");
		memcpy(dst, src, dims * word);
		return;
	}

	switch (bitdepth) {
	case 8: strip_interleave8(dst, src, dims, planes); break;
	default:
		strip_interleave_inline(dst, src, dims, planes, word);
	}
}

void strip_spread(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t ch) {
	for (size_t x = 0; x < width; ++x) {
		dst[x*ch] = src[x];
	}
}

void strip_swizzle(uint8_t *dst, const uint8_t *src, const size_t w,
const size_t h, const size_t ch, const size_t bitdepth, const size_t alignment,
const enum pix_layout src_layout, const enum pix_layout dst_layout) {
	uint8_t dst_swizzle[4];
	uint8_t src_swizzle[4];
	pix_swizzle_mask(dst_swizzle, dst_layout);
	pix_swizzle_mask(src_swizzle, src_layout);

	const size_t elem_size = bitdepth / 8;
	const size_t padding = (alignment - (w * ch * elem_size % alignment))
		% alignment;

	for (size_t y = 0; y < h; ++y) {
		for (size_t x = 0; x < w; ++x) {
			uint8_t buf[8*4];
			for (uint8_t z = 0; z < ch; ++z) {
				const size_t dst_z = dst_swizzle[z] * elem_size;
				const size_t src_z = src_swizzle[z] * elem_size;
				memcpy(buf + dst_z, src + src_z, elem_size);
			}
			const size_t span = elem_size * ch;
			memcpy(dst, buf, span);
			dst += span;
			src += span;
		}
		src += padding;
	}
}
