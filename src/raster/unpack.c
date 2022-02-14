#include <string.h>
#include <limits.h>

#include "../common.h"
#include "bit.h"
#include "unpack.h"

static uint8_t get_unpackdepth(const uint8_t depth) {
	if (depth && depth < 16) {
		return (depth > 8) ? 16 : 8;
	}
	return 0;
}

// Unpack 2^n-bits onto u8
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

// Expand 2^n-bit to 8-bit
static void expand4(const uint_fast8_t byte, uint8_t *dst, const size_t nr) {
	const int s = 0xff / 0x0f;
	switch (nr) {
	case 2:
		dst[1] = (uint8_t)( (byte & 0x0f) * s );
		// fallthrough
	case 1:
		dst[0] = (uint8_t)( (byte >> 4) * s);
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
const uint8_t *restrict src, const size_t width, const uint8_t bitdepth,
const enum unpack_op op, const enum pix_attr attr) {
	const size_t ipb = 8 / bitdepth;

	const size_t bytes = width / ipb;
	const size_t remainer = width % ipb;
	for (size_t x = 0; x < bytes; ++x) {
		const size_t o = x * ipb;
		uint8_t byte = src[x];
		if (attr == pix_inverted) {
			byte ^= 0xff;
		}
		select_unpack(byte, dst + o, ipb, op, bitdepth);
	}
	if (remainer) {
		uint8_t byte = src[bytes];
		if (attr == pix_inverted) {
			byte ^= 0xff;
		}
		select_unpack(byte, dst + width - remainer, remainer, op,
			bitdepth);
	}
}

// Inline the common version in each variant for maximum speed
static void strip_invert4(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 4, op_expand, pix_inverted);
}

static void strip_invert2(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 2, op_expand, pix_inverted);
}

static void strip_invert1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 1, op_expand, pix_inverted);
}

static void strip_expand4(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 4, op_expand, pix_normal);
}

static void strip_expand2(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 2, op_expand, pix_normal);
}

static void strip_expand1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 1, op_expand, pix_normal);
}

static void strip_unpack4(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 4, op_unpack, pix_normal);
}

static void strip_unpack2(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 2, op_unpack, pix_normal);
}

static void strip_unpack1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 1, op_unpack, pix_normal);
}


// Unpack any bitdepth < 16
static inline void strip_small_common(void *restrict dst,
const void *restrict src, const size_t width, const uint8_t bitdepth,
const enum unpack_op op, const enum pix_attr attr) {
	const uint8_t outdepth = get_unpackdepth(bitdepth);

	const uint_fast32_t mask = (1u << bitdepth) - 1;
	const uint_fast32_t outmask = (1u << outdepth) - 1;
	const uint_fast32_t scale = (outmask << 16) / mask + 1;
	for (size_t x = 0; x < width; ++x) {
		uint_fast32_t pix = bit_getn(src, x*bitdepth, bitdepth);
		if (op == op_expand) {
			if (attr == pix_inverted) {
				pix ^= mask;
			}
			pix = (pix * scale) >> 16;
		}
		switch (outdepth) {
		case 8: ((uint8_t *)dst)[x] = (uint8_t)pix; break;
		case 16: ((uint16_t *)dst)[x] = (uint16_t)pix; break;
		}
	}
}

static void strip_sm_unpack(void *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth) {
	strip_small_common(dst, src, n, bitdepth, op_unpack, pix_normal);
}

static void strip_sm_expand(void *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth) {
	strip_small_common(dst, src, n, bitdepth, op_expand, pix_normal);
}

static void strip_sm_invert(void *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth) {
	strip_small_common(dst, src, n, bitdepth, op_expand, pix_inverted);
}


// Pack 2^n-bits onto u16
static inline uint16_t select_wordpack(const void *src, const size_t x,
const uint8_t bytedepth) {
	if (bytedepth == 64) {
		return (uint16_t)( ((uint64_t *)src)[x] >> (64 - 16) );
	}
	return (uint16_t)( ((uint32_t *)src)[x] >> (32 - 16) );
}

static inline void strip_wordpack_common(uint16_t *dst, const void *restrict src,
const size_t width, const uint8_t bitdepth, const enum pix_attr attr) {
	const uint16_t invert = (attr == pix_inverted) ? 0xffff : 0x0000;
	for (size_t x = 0; x < width; ++x) {
		dst[x] = (uint16_t)(
			select_wordpack(src, x, bitdepth) ^ invert
		);
	}
}

static void strip_pack32_16(uint16_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_wordpack_common(dst, src, n, 32, pix_normal);
}

static void strip_pack64_16(uint16_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_wordpack_common(dst, src, n, 64, pix_normal);
}

static void strip_pack32_inv16(uint16_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_wordpack_common(dst, src, n, 32, pix_inverted);
}

static void strip_pack64_inv16(uint16_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_wordpack_common(dst, src, n, 64, pix_inverted);
}

static void strip_pack64f_32f(float *dst, const double *src, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		dst[i] = (float)(src[i]);
	}
}


// Pack any bitdepth > 16
static inline void strip_big_common(uint16_t *restrict dst,
const void *restrict src, const size_t width, const uint8_t bitdepth,
const enum pix_attr attr) {
	const uint_fast32_t mask = USHRT_MAX;
	for (size_t x = 0; x < width; ++x) {
		uint_fast32_t pix = bit_getn(src, x*bitdepth, 16);
		if (attr == pix_inverted) {
			pix ^= mask;
		}
		dst[x] = (uint16_t)pix;
	}
}

static void strip_big_pack(uint16_t *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth) {
	strip_big_common(dst, src, n, bitdepth, pix_normal);
}

static void strip_big_invert(uint16_t *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth) {
	strip_big_common(dst, src, n, bitdepth, pix_inverted);
}


// Irregular packings
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
const uint16_t *restrict src, const size_t n) {
	for (size_t x = 0; x < n; ++x) {
		expand1555(src[x], dst + x*4);
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
const size_t n) {
	for (size_t x = 0; x < n; ++x) {
		expand332(src[x], dst + x*3);
	}
}

static void strip_invert(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	for (size_t x = 0; x < n; ++x) {
		dst[x] = src[x] ^ 0xff;
	}
}

void unpack_strip(void *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth, const enum pix_attr attr,
const enum unpack_op op) {
	switch (attr) {
	case pix_normal:
		switch (op) {
		case op_noop: break;
		case op_unpack:
			switch (bitdepth) {
			case 1: strip_unpack1(dst, src, n); break;
			case 2: strip_unpack2(dst, src, n); break;
			case 4: strip_unpack4(dst, src, n); break;
			default: strip_sm_unpack(dst, src, n, bitdepth);
			}
			break;
		case op_expand:
			switch (bitdepth) {
			case 1: strip_expand1(dst, src, n); break;
			case 2: strip_expand2(dst, src, n); break;
			case 4: strip_expand4(dst, src, n); break;
			default: strip_sm_expand(dst, src, n, bitdepth);
			}
			break;
		case op_pack:
			switch (bitdepth) {
			case 32: strip_pack32_16(dst, src, n); break;
			case 64: strip_pack64_16(dst, src, n); break;
			default: strip_big_pack(dst, src, n, bitdepth);
			}
			break;
		}
		break;
	case pix_inverted:
		switch (op) {
		case op_noop:
		case op_unpack: break;
		case op_expand:
			switch (bitdepth) {
			case 1: strip_invert1(dst, src, n); break;
			case 2: strip_invert2(dst, src, n); break;
			case 4: strip_invert4(dst, src, n); break;
			default:
				if (bitdepth % 8) {
					strip_sm_invert(dst, src, n, bitdepth);
				} else {
					strip_invert(dst, src, n*(bitdepth/8));
				}
				break;
			}
			break;
		case op_pack:
			switch (bitdepth) {
			case 32: strip_pack32_inv16(dst, src, n); break;
			case 64: strip_pack64_inv16(dst, src, n); break;
			default: strip_big_invert(dst, src, n, bitdepth);
			}
			break;
		}
		break;
	case pix_float:
		if (op == op_pack && bitdepth == 64) {
			strip_pack64f_32f(dst, src, n);
		}
		break;
	case pix_packing_332:
		if (op == op_expand && bitdepth == 8) {
			strip_expand332(dst, src, n);
		}
		break;
	case pix_packing_1555:
		if (op == op_expand && bitdepth == 16) {
			strip_expand1555(dst, src, n);
		}
		break;
	}
}

void unpack_or_copy_strip(void *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth, const enum pix_attr attr,
const enum unpack_op op) {
	if (op == op_noop) {
		memcpy(dst, src, scanline_length(n, bitdepth, 1));
	} else {
		unpack_strip(dst, src, n, bitdepth, attr, op);
	}
}

uint8_t unpack_depth(const uint8_t bitdepth, const enum pix_attr attr,
const enum unpack_op op) {
	uint8_t outdepth = 0;
	switch (attr) {
	case pix_normal:
		switch (op) {
		case op_noop: break;
		case op_unpack:
		case op_expand:
			outdepth = get_unpackdepth(bitdepth);
			break;
		case op_pack:
			if (bitdepth > 16) {
				outdepth = 16;
			}
			break;
		}
		break;
	case pix_inverted:
		switch (op) {
		case op_noop:
		case op_unpack: break;
		case op_expand:
			if (bitdepth % 8) {
				outdepth = get_unpackdepth(bitdepth);
			} else {
				outdepth = bitdepth;
			}
			break;
		case op_pack:
			if (bitdepth > 16) {
				outdepth = 16;
			}
			break;
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

size_t unpack_stride(const size_t n, const uint8_t bitdepth,
const enum pix_attr attr, const enum unpack_op op) {
	uint8_t outdepth = unpack_depth(bitdepth, attr, op);
	if (outdepth) {
		return scanline_length(n, outdepth, 1);
	}
	return 0;
}

void strip_spread(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t ch) {
	for (size_t x = 0; x < width; ++x) {
		dst[x*ch] = src[x];
	}
}

void strip_swizzle(void *data, const size_t w, const uint8_t ch,
const size_t elem_size, const enum pix_layout dst_layout,
const enum pix_layout src_layout) {
	uint8_t dst_swizzle[4];
	uint8_t src_swizzle[4];
	pix_swizzle_mask(dst_swizzle, dst_layout);
	pix_swizzle_mask(src_swizzle, src_layout);

	const size_t pix_size = elem_size * ch;
	uint8_t *d = data;
	for (size_t x = 0; x < w; ++x) {
		uint8_t buf[8*4];
		for (uint8_t z = 0; z < ch; ++z) {
			const size_t dst_z = dst_swizzle[z] * elem_size;
			const size_t src_z = src_swizzle[z] * elem_size;
			memcpy(buf + dst_z, d + src_z, elem_size);
		}
		memcpy(d, buf, pix_size);
		d += pix_size;
	}
}
