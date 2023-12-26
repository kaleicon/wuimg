// SPDX-License-Identifier: 0BSD
#include <string.h>
#include <limits.h>

#include "misc/bit.h"
#include "misc/endian.h"
#include "raster/strip.h"
#include "raster/unpack.h"

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
const enum unpack_op op, const uint8_t xor) {
	const size_t ppb = 8 / bitdepth;

	const size_t bytes = width / ppb;
	const size_t remainer = width % ppb;
	for (size_t x = 0; x < bytes; ++x) {
		const size_t o = x * ppb;
		const uint8_t byte = src[x] ^ xor;
		select_unpack(byte, dst + o, ppb, op, bitdepth);
	}
	if (remainer) {
		const uint8_t byte = src[bytes] ^ xor;
		select_unpack(byte, dst + width - remainer, remainer, op,
			bitdepth);
	}
}

static void strip_xor4(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n, const enum pix_attr attr) {
	const uint8_t xor = (attr == pix_signed) ? 0x88 : 0xff;
	strip_common(dst, src, n, 4, op_expand, xor);
}

static void strip_xor2(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n, const enum pix_attr attr) {
	const uint8_t xor = (attr == pix_signed) ? 0xaa : 0xff;
	strip_common(dst, src, n, 2, op_expand, xor);
}

static void strip_xor1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 1, op_expand, 0xff);
}

static void strip_expand4(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 4, op_expand, 0);
}

static void strip_expand2(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 2, op_expand, 0);
}

static void strip_expand1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 1, op_expand, 0);
}

static void strip_unpack4(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 4, op_unpack, 0);
}

static void strip_unpack2(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 2, op_unpack, 0);
}

static void strip_unpack1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 1, op_unpack, 0);
}

static void strip_unpack_xor1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 1, op_unpack, 0xff);
}

static void strip_invert(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n, const uint8_t bytedepth, const enum pix_attr attr) {
	if (attr == pix_inverted) {
		for (size_t x = 0; x < n; ++x) {
			dst[x] = src[x] ^ 0xff;
		}
	} else { // pix_signed
		const size_t step = (which_end() == little_endian)
			? bytedepth - 1 : 0;
		for (size_t x = 0; x < n; x += bytedepth) {
			for (size_t i = 0; i < bytedepth; ++i) {
				const uint8_t c = src[x+i];
				if (i == step) {
					dst[x+i] = c ^ 0x80;
				} else {
					dst[x+i] = c;
				}
			}
		}
	}
}

// Unpack any bitdepth < 16 and non-power-of-two
static inline void strip_small_common(void *restrict dst,
const void *restrict src, const size_t width, const uint8_t bitdepth,
const enum pix_attr attr, const enum unpack_op op) {
	const uint8_t outdepth = (bitdepth > 8) ? 16 : 8;

	const uint32_t inrange = (1u << bitdepth) - 1;
	const uint32_t outrange = (1u << outdepth) - 1;
	const uint32_t scale = (outrange << 16) / inrange + 1;
	uint32_t xor;
	switch (attr) {
	case pix_signed: xor = (1u << (bitdepth - 1)); break;
	case pix_inverted: xor = inrange; break;
	default: xor = 0; break;
	}
	for (size_t x = 0; x < width; ++x) {
		uint32_t pix = bit_getn(src, x*bitdepth, bitdepth) ^ xor;
		if (op == op_expand) {
			pix = (pix * scale) >> 16;
		}
		switch (outdepth) {
		case 8: ((uint8_t *)dst)[x] = (uint8_t)pix; break;
		case 16: ((uint16_t *)dst)[x] = (uint16_t)pix; break;
		}
	}
}

static void strip_sm_unpack(void *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth, const enum pix_attr attr) {
	strip_small_common(dst, src, n, bitdepth, attr, op_unpack);
}

static void strip_sm_expand(void *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth, const enum pix_attr attr) {
	strip_small_common(dst, src, n, bitdepth, attr, op_expand);
}


// Pack any bitdepth > 16
static inline uint16_t select_wordpack(const void *src, const size_t x,
const uint8_t bitdepth) {
	switch (bitdepth) {
	case 32: return (uint16_t)( ((uint32_t *)src)[x] >> (32 - 16) );
	case 64: return (uint16_t)( ((uint64_t *)src)[x] >> (64 - 16) );
	}
	return (uint16_t)bit_getn(src, x*bitdepth, 16);
}

static inline void strip_wordpack_common(uint16_t *dst, const void *restrict src,
const size_t width, const uint8_t bitdepth, const enum pix_attr attr) {
	uint16_t xor;
	switch (attr) {
	case pix_signed: xor = 0x8000; break;
	case pix_inverted: xor = 0xffff; break;
	default: xor = 0; break;
	}
	for (size_t x = 0; x < width; ++x) {
		dst[x] = select_wordpack(src, x, bitdepth) ^ xor;
	}
}

static void strip_pack32_16(uint16_t *restrict dst, const void *restrict src,
const size_t n, const enum pix_attr attr) {
	strip_wordpack_common(dst, src, n, 32, attr);
}

static void strip_pack64_16(uint16_t *restrict dst, const void *restrict src,
const size_t n, const enum pix_attr attr) {
	strip_wordpack_common(dst, src, n, 64, attr);
}

static void strip_pack_16(uint16_t *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth, const enum pix_attr attr) {
	strip_wordpack_common(dst, src, n, bitdepth, attr);
}

static void strip_pack64f_32f(float *dst, const double *src, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		dst[i] = (float)(src[i]);
	}
}


// Irregular packings
static void strip_expand1555(uint8_t *restrict dst,
const upack1555_t *restrict src, const size_t w) {
	const uint8_t mask = (1 << 5) - 1;
	const uint8_t p = 6;
	const uint16_t scale = (UCHAR_MAX << p) / mask + 1;

	for (size_t x = 0; x < w; ++x) {
		const upack1555_t word = src[x];
		for (int n = 0; n < 3; ++n) {
			const int m = n*5;
			dst[x*4 + (size_t)n] = (uint8_t)(
				(scale * (word & (mask << m))) >> (p+m)
			);
		}
		dst[x*4 + 3] = (word >> 15) ? 0xff : 0x00;
	}
}

static void strip_expand332(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t w) {
	const int rgscale = (UCHAR_MAX << 7) / 0x07 + 1;
	const int bscale = 0xff / 3;

	for (size_t x = 0; x < w; ++x) {
		const uint8_t byte = src[x];
		const int r = byte >> 5;
		const int g = (byte >> 2) & 0x07;
		const int b = byte & 0x03;

		dst[x*3] = (uint8_t)((r * rgscale) >> 7);
		dst[x*3 + 1] = (uint8_t)((g * rgscale) >> 7);
		dst[x*3 + 2] = (uint8_t)(b * bscale);
	}
}

void unpack_strip(void *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth, const enum pix_attr attr,
const enum unpack_op op) {
	switch (op) {
	case op_noop: break;
	case op_unpack:
		switch (attr) {
		case pix_normal:
			switch (bitdepth) {
			case 1: strip_unpack1(dst, src, n); return;
			case 2: strip_unpack2(dst, src, n); return;
			case 4: strip_unpack4(dst, src, n); return;
			}
			break;
		case pix_inverted:
			switch (bitdepth) {
			case 1: strip_unpack_xor1(dst, src, n); return;
			}
			break;
		default: return;
		}
		strip_sm_unpack(dst, src, n, bitdepth, attr);
		break;
	case op_expand:
		switch (attr) {
		case pix_normal:
			switch (bitdepth) {
			case 1: strip_expand1(dst, src, n); break;
			case 2: strip_expand2(dst, src, n); break;
			case 4: strip_expand4(dst, src, n); break;
			default: strip_sm_expand(dst, src, n, bitdepth, attr);
			}
			break;
		case pix_signed:
		case pix_inverted:
			switch (bitdepth) {
			case 1: strip_xor1(dst, src, n); break;
			case 2: strip_xor2(dst, src, n, attr); break;
			case 4: strip_xor4(dst, src, n, attr); break;
			default:
				if (bitdepth % 8) {
					strip_sm_expand(dst, src, n, bitdepth, attr);
				} else {
					strip_invert(dst, src, n, bitdepth/8, attr);
				}
				break;
			}
			break;
		case pix_float: break;
		case pix_pack_332:
			if (bitdepth == 8) {
				strip_expand332(dst, src, n);
			}
			break;
		case pix_pack_1555:
			if (bitdepth == 16) {
				strip_expand1555(dst, src, n);
			}
			break;
		}
		break;
	case op_pack:
		switch (attr) {
		case pix_normal:
		case pix_signed:
		case pix_inverted:
			switch (bitdepth) {
			case 32: strip_pack32_16(dst, src, n, attr); break;
			case 64: strip_pack64_16(dst, src, n, attr); break;
			default: strip_pack_16(dst, src, n, bitdepth, attr);
			}
			break;
		case pix_float:
			if (bitdepth == 64) {
				strip_pack64f_32f(dst, src, n);
			}
			break;
		case pix_pack_332:
		case pix_pack_1555:
			break;
		}
		break;
	}
}

void unpack_or_copy_strip(void *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth, const enum pix_attr attr,
const enum unpack_op op) {
	if (op == op_noop) {
		const size_t len = strip_base(n, bitdepth);
		switch (attr) {
		case pix_signed:
		case pix_inverted:
			strip_invert(dst, src, len, bitdepth/8, pix_inverted);
			break;
		default:
			memcpy(dst, src, len);
		}
	} else {
		unpack_strip(dst, src, n, bitdepth, attr, op);
	}
}

static uint8_t get_unpackdepth(const uint8_t depth) {
	if (depth && depth < 16) {
		return (depth > 8) ? 16 : 8;
	}
	return 0;
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
	case pix_signed:
	case pix_inverted:
		switch (op) {
		case op_noop: break;
		case op_unpack:
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
	case pix_pack_332:
		if (op == op_expand && bitdepth == 8) {
			outdepth = 24;
		}
		break;
	case pix_pack_1555:
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
		return strip_base(n, outdepth);
	}
	return 0;
}


// Convert signed to unsigned and scale
static uint64_t ams(int32_t c, uint32_t add, uint64_t mul, uint8_t shr) {
	return (((uint32_t)c + add) * mul) >> shr;
}
static void design8(uint8_t *dst, const int8_t *src, const size_t w,
const uint64_t mul, const uint32_t add) {
	for (size_t x = 0; x < w; ++x) {
		dst[x] = (uint8_t)ams(src[x], (uint8_t)add, (uint16_t)mul, 8);
	}
}
static void design16(uint16_t *dst, const int16_t *src, const size_t w,
const uint64_t mul, const uint32_t add) {
	for (size_t x = 0; x < w; ++x) {
		dst[x] = (uint16_t)ams(src[x], (uint16_t)add, (uint32_t)mul, 16);
	}
}
static void design32(uint32_t *dst, const int32_t *src, const size_t w,
const uint64_t mul, const uint32_t add) {
	for (size_t x = 0; x < w; ++x) {
		dst[x] = (uint32_t)ams(src[x], add, mul, 32);
	}
}

// Scale
static uint64_t msx(uint32_t c, uint64_t mul, uint8_t shr, uint32_t xor) {
	return ((c * mul) >> shr) ^ xor;
}
static void scale8(uint8_t *dst, const uint8_t *src, const size_t w,
const uint64_t mul, const uint32_t xor) {
	for (size_t x = 0; x < w; ++x) {
		dst[x] = (uint8_t)msx(src[x], (uint16_t)mul, 8, xor);
	}
}
static void scale16(uint16_t *dst, const uint16_t *src, const size_t w,
const uint64_t mul, const uint32_t xor) {
	for (size_t x = 0; x < w; ++x) {
		dst[x] = (uint16_t)msx(src[x], (uint32_t)mul, 16, xor);
	}
}
static void scale32(uint32_t *dst, const uint32_t *src, const size_t w,
const uint64_t mul, const uint32_t xor) {
	for (size_t x = 0; x < w; ++x) {
		dst[x] = (uint32_t)msx(src[x], mul, 32, xor);
	}
}

void repack_scale(void *dst, const void *src, const size_t width,
const struct scale_info info, const enum pix_attr attr) {
	const uint64_t mul = info.mul;
	const uint8_t shr = info.bitdepth;
	uint32_t xor = 0;
	switch (attr) {
	case pix_signed:
		;const uint32_t add = info.add;
		switch (shr) {
		case 8: design8(dst, src, width, mul, add); break;
		case 16: design16(dst, src, width, mul, add); break;
		case 32: design32(dst, src, width, mul, add); break;
		}
		return;
	case pix_inverted:
		xor = ~0u;
		break;
	case pix_normal:
		if (!info.scale) {
			if (dst != src) {
				memcpy(dst, src, shr/8 * width);
			}
			return;
		}
		break;
	case pix_float:
	case pix_pack_332:
	case pix_pack_1555:
		return;
	}

	switch (shr) {
	case 8: scale8(dst, src, width, mul, xor); break;
	case 16: scale16(dst, src, width, mul, xor); break;
	case 32: scale32(dst, src, width, mul, xor); break;
	}
}

struct scale_info repack_scale_info(const uint32_t maxval,
const uint8_t outdepth) {
	const uint32_t range = bit_set32(outdepth);
	return (struct scale_info) {
		.bitdepth = outdepth,
		.scale = maxval != range,
		.add = maxval/2 + 1,
		.mul = ((uint64_t)range << outdepth) / maxval + 1,
	};
}
