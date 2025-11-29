// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <string.h>
#include <limits.h>

#include "misc/bit.h"
#include "raster/strip.h"
#include "raster/unpack.h"

const char * unpack_op_str(const enum unpack_op op) {
	switch (op) {
	case op_noop: return "noop";
	case op_repack: return "repack";
	case op_bitfield: return "bitfield";
	}
	return "???";
}

// Unpack 2^n-bit to 8-bit
static void unpack4(const uint8_t byte, uint8_t *dst, const size_t nr,
const enum endianness e) {
	if (e == big_endian) {
		switch (nr) {
		case 2: dst[1] = (uint8_t)(byte & 0x0f); // fallthrough
		case 1: dst[0] = (uint8_t)(byte >> 4);
		}
	} else {
		switch (nr) {
		case 2: dst[0] = (uint8_t)(byte & 0x0f); // fallthrough
		case 1: dst[1] = (uint8_t)(byte >> 4);
		}
	}
}

static void unpack2(const uint8_t byte, uint8_t *dst, const size_t nr,
const enum endianness e) {
	if (e == big_endian) {
		switch (nr) {
		case 4: dst[3] = (uint8_t)(byte & 0x03); // fallthrough
		case 3: dst[2] = (uint8_t)((byte >> 2) & 0x03); // fallthrough
		case 2: dst[1] = (uint8_t)((byte >> 4) & 0x03); // fallthrough
		case 1: dst[0] = (uint8_t)(byte >> 6);
		}
	} else {
		switch (nr) {
		case 4: dst[0] = (uint8_t)(byte & 0x03); // fallthrough
		case 3: dst[1] = (uint8_t)((byte >> 2) & 0x03); // fallthrough
		case 2: dst[2] = (uint8_t)((byte >> 4) & 0x03); // fallthrough
		case 1: dst[3] = (uint8_t)(byte >> 6);
		}
	}
}

static void unpack1(const uint8_t byte, uint8_t *dst, const size_t nr,
const enum endianness e) {
	for (size_t i = 0; i < nr; ++i) {
		if (e == big_endian) {
			dst[i] = (byte & (0x80 >> i)) ? 0x01 : 0x00;
		} else {
			dst[i] = (byte & (0x01 << i)) ? 0x01 : 0x00;
		}
	}
}

// Have the compiler inline one of the above
static inline void select_unpack(const uint8_t byte, uint8_t *dst,
const size_t nr, const enum endianness e, const size_t bitdepth) {
	switch (bitdepth) {
	case 1: unpack1(byte, dst, nr, e); break;
	case 2: unpack2(byte, dst, nr, e); break;
	case 4: unpack4(byte, dst, nr, e); break;
	}
}

// The above but looping
static inline void strip_common(uint8_t *restrict dst,
const uint8_t *restrict src, const size_t width, const uint8_t bitdepth,
const uint8_t xor, const enum endianness e) {
	const size_t ppb = 8 / bitdepth;

	const size_t bytes = width / ppb;
	const size_t remainer = width % ppb;
	for (size_t x = 0; x < bytes; ++x) {
		const size_t o = x * ppb;
		const uint8_t byte = src[x] ^ xor;
		select_unpack(byte, dst + o, ppb, e, bitdepth);
	}
	if (remainer) {
		const uint8_t byte = src[bytes] ^ xor;
		select_unpack(byte, dst + width - remainer, remainer, e,
			bitdepth);
	}
}

static void strip_unpack4b(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 4, 0, big_endian);
}
static void strip_unpack2b(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 2, 0, big_endian);
}
static void strip_unpack1b(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 1, 0, big_endian);
}

static void strip_unpack4l(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 4, 0, little_endian);
}
static void strip_unpack1l(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n) {
	strip_common(dst, src, n, 1, 0, little_endian);
}

// Signed to unsigned
static void strip_design8(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t w, const uint32_t add) {
	for (size_t x = 0; x < w; ++x) {
		dst[x] = (uint8_t)(src[x] + add);
	}
}

static void strip_design16(uint16_t *restrict dst, const uint16_t *restrict src,
const size_t w, const uint32_t add) {
	for (size_t x = 0; x < w; ++x) {
		dst[x] = (uint16_t)(src[x] + add);
	}
}

static void strip_design(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t n, const uint8_t bytedepth) {
	const size_t step = (which_end() == little_endian)
		? bytedepth - 1 : 0;
	for (size_t x = 0; x < n; x += bytedepth) {
		for (size_t i = 0; i < bytedepth; ++i) {
			dst[x+i] = src[x+i] ^ (i == step ? 0x80 : 0x00);
		}
	}
}

// Unpack any bitdepth < 16 and non-power-of-two
static void strip_unpack_any(void *restrict dst,
const void *restrict src, const size_t width, const uint8_t bitdepth,
const enum pix_attr attr, uint8_t bitrange) {
	uint32_t xor = 0;
	switch (attr) {
	case pix_signed: xor = (1 << (bitrange - 1)); break;
	default: bitrange = bitrange > bitdepth ? bitdepth : bitrange; break;
	}

	const bool highdepth = (bitdepth > 8);
	const uint8_t off = 0;//bitdepth - bitrange;
	for (size_t x = 0; x < width; ++x) {
		uint32_t pix = bit_getn(src, x*bitdepth+off, bitrange) ^ xor;
		if (highdepth) {
			((uint16_t *)dst)[x] = (uint16_t)pix;
		} else {
			((uint8_t *)dst)[x] = (uint8_t)pix;
		}
	}
}


// Pack any bitdepth > 16
static inline uint16_t select_wordpack(const void *src, const size_t x,
const uint8_t bitdepth) {
	switch (bitdepth) {
	case 32: return (uint16_t)( ((const uint32_t *)src)[x] >> (32 - 16) );
	case 64: return (uint16_t)( ((const uint64_t *)src)[x] >> (64 - 16) );
	}
	return (uint16_t)bit_getn(src, x*bitdepth, 16);
}

static inline void strip_wordpack_common(uint16_t *dst, const void *restrict src,
const size_t width, const uint8_t bitdepth, const enum pix_attr attr) {
	uint16_t xor = 0;
	switch (attr) {
	case pix_signed: xor = 0x8000; break;
	default: break;
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

static void strip_design32_pack16(uint16_t *restrict dst,
const uint32_t *restrict src, const size_t w, const uint8_t range) {
	const uint32_t add = 1 << (range - 1);
	const uint32_t shl = (range > 16) ? range - 16 : 16;
	for (size_t x = 0; x < w; ++x) {
		dst[x] = (uint16_t)((src[x] + add) >> shl);
	}
}

static void strip_pack64f_32f(float *dst, const double *src, const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		dst[i] = (float)(src[i]);
	}
}

static uint8_t get_range(const uint8_t bitdepth, const void *arg) {
	return arg ? *(const uint8_t *)arg : bitdepth;
}

void unpack_strip(void *restrict dst, const void *restrict src,
const size_t n, const uint8_t bitdepth, const enum pix_attr attr,
const enum endianness bit, const enum unpack_op op, const void *arg) {
	switch (op) {
	case op_noop:
		memcpy(dst, src, strip_base(n, bitdepth));
		break;
	case op_repack:
		;const uint8_t range = get_range(bitdepth, arg);
		switch (attr) {
		case pix_normal:
			if (bit == big_endian) {
				switch (bitdepth) {
				case 1: strip_unpack1b(dst, src, n); return;
				case 2: strip_unpack2b(dst, src, n); return;
				case 4: strip_unpack4b(dst, src, n); return;
				case 32: strip_pack32_16(dst, src, n, attr);
					return;
				case 64: strip_pack64_16(dst, src, n, attr);
					return;
				}
			} else {
				switch (bitdepth) {
				case 1: strip_unpack1l(dst, src, n); return;
				case 4: strip_unpack4l(dst, src, n); return;
				}
			}
			break;
		case pix_signed:
			;const uint32_t add = 1 << (range - 1);
			switch (bitdepth) {
			case 8: strip_design8(dst, src, n, add); return;
			case 16: strip_design16(dst, src, n, add); return;
			case 32:
				strip_design32_pack16(dst, src, n, range);
				return;
			case 64: strip_pack64_16(dst, src, n, attr); return;
			}
			break;
		case pix_float:
			if (bitdepth == 64) {
				strip_pack64f_32f(dst, src, n);
			}
			return;
		}
		if (bitdepth > 16) {
			strip_pack_16(dst, src, n, bitdepth, attr);
		} else {
			strip_unpack_any(dst, src, n, bitdepth, attr, range);
		}
		break;
	case op_bitfield:
		bitfield_unpack(arg, dst, src, n);
		break;
	}
}

static uint8_t unpack_depth(const uint8_t bitdepth, const enum pix_attr attr,
const enum unpack_op op, const void *arg) {
	switch (op) {
	case op_noop:
		return bitdepth;
	case op_repack:
		switch (attr) {
		case pix_normal:
			if (bitdepth > 16 || bitdepth % 8) {
				return bitdepth > 8 ? 16 : 8;
			}
			break;
		case pix_signed:
			if (bitdepth) {
				return bitdepth > 8 ? 16 : 8;
			}
			break;
		case pix_float:
			if (bitdepth == 64) {
				return 32;
			}
			break;
		}
		break;
	case op_bitfield:
		;const struct bitfield *bf = arg;
		return bf->outdepth * bf->ch;
	}
	return 0;
}

size_t unpack_stride(const size_t n, const uint8_t bitdepth,
const enum pix_attr attr, const enum unpack_op op, const void *arg) {
	uint8_t outdepth = unpack_depth(bitdepth, attr, op, arg);
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

static void exact_mul(uint32_t *dst, const uint32_t *src, const size_t w,
const uint32_t mul, const uint8_t depth) {
	const size_t div = 2 - depth;
	const size_t items = w >> div;
	const size_t remain = w - (items << div);
	for (size_t x = 0; x < items; ++x) {
		dst[x] = src[x] * mul;
	}
	if (remain) {
		uint32_t tmp = 0;
		memcpy(&tmp, src, remain);
		tmp *= mul;
		memcpy(dst + items, &tmp, remain);
	}
}

void remap_scale(void *dst, const void *src, const size_t n,
const struct remap_info info) {
	const uint8_t depth = info.depth_sh;
	const uint64_t mul = info.mul;
	const uint32_t var = info.var;
	if (info.scale) {
		switch (info.attr) {
		case pix_signed:
			switch (depth) {
			case 0: design8(dst, src, n, mul, var); break;
			case 1: design16(dst, src, n, mul, var); break;
			case 2: design32(dst, src, n, mul, var); break;
			}
			return;
		case pix_normal:
			if (info.exact) {
				exact_mul(dst, src, n, var, depth);
				return;
			}
			switch (depth) {
			case 0: scale8(dst, src, n, mul, var); break;
			case 1: scale16(dst, src, n, mul, var); break;
			case 2: scale32(dst, src, n, mul, var); break;
			}
		case pix_float:
			break;
		}
	} else {
		switch (info.attr) {
		case pix_normal:
			if (dst != src) {
				memcpy(dst, src, n << depth);
			}
			break;
		case pix_signed:
			strip_design(dst, src, n, (uint8_t)(1 << depth));
			break;
		case pix_float:
			break;
		}
	}
}

struct remap_info remap_scale_info(const uint32_t maxval,
const uint8_t outdepth, const enum pix_attr attr) {
	const uint32_t range = bit_set32(outdepth);
	struct remap_info info = (struct remap_info) {
		.scale = range != maxval,
		.exact = range % maxval == 0 && attr == pix_normal,
		.depth_sh = bit_min_wordsize_log2(outdepth),
		.attr = attr,
	};
	switch (attr) {
	case pix_signed:
		info.var = maxval/2 + 1;
		break;
	case pix_normal: case pix_float:
		break;
	}

	if (info.exact) {
		info.var = range / maxval;
	} else {
		info.mul = ((uint64_t)range << outdepth) / maxval + 1;
	}
	return info;
}
