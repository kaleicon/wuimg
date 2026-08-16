// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <string.h>

#include "misc/bit.h"
#include "misc/endian.h"
#include "misc/math.h"

uint16_t bit_rev16(uint16_t b) {
	b = (uint16_t)( (b & 0xaaaa) >> 1 | (b & 0x5555) << 1 );
	b = (uint16_t)( (b & 0xcccc) >> 2 | (b & 0x3333) << 2 );
	b = (uint16_t)( (b & 0xf0f0) >> 4 | (b & 0x0f0f) << 4 );
	return (uint16_t)(b >> 8 | b << 8);
}

uint32_t bit_rev32(uint32_t b) {
	b = (b & 0xaaaaaaaa) >> 1 | (b & 0x55555555) << 1;
	b = (b & 0xcccccccc) >> 2 | (b & 0x33333333) << 2;
	b = (b & 0xf0f0f0f0) >> 4 | (b & 0x0f0f0f0f) << 4;
	b = (b & 0xff00ff00) >> 8 | (b & 0x00ff00ff) << 8;
	return (b >> 16 | b << 16);
}

uint32_t bit_ctz32(uint32_t bits) {
	uint32_t n = sizeof(bits)*8;
	while (bits) {
		--n;
		bits <<= 1;
	}
	return n;
}

uint32_t bit_cto32(uint32_t bits) {
	uint32_t n = 0;
	while (bits & 1) {
		++n;
		bits >>= 1;
	}
	return n;
}

uint32_t bit_clz32(uint32_t bits) {
	uint32_t n = sizeof(bits)*8;
	while (bits) {
		--n;
		bits >>= 1;
	}
	return n;
}

uint32_t bit_clo32(uint32_t bits) {
	return bit_clz32(~bits);
}

uint32_t bit_set32(const uint32_t bits) {
	const uint32_t ones = 0u - (bool)bits;
	return ones >> (sizeof(ones)*8 - bits);
}


uint8_t bit_min_wordsize_log2(const unsigned bits) {
	return (uint8_t)(ulog2(umax(bits, 8) - 1) - 2);
}

uint8_t bit_min_wordsize_bits(const unsigned bits) {
	return (uint8_t)(8 << bit_min_wordsize_log2(bits));
}


uint32_t bit_getn(const void *stream, const size_t pos, size_t n) {
	const uint32_t mask = (1u << n) - 1;

	size_t o = pos / 8;
	size_t i = pos % 8;

	const uint8_t *s = (const uint8_t *)stream;
	n += i;
	uint32_t word = 0;
	while (n > 8) {
		word = word << 8 | s[o];
		++o;
		n -= 8;
	}
	word = word << 8 | s[o];
	return (uint32_t)(word >> ((8 - n)%8)) & mask;
}

bool bit_get(const void *stream, const size_t pos) {
	const uint8_t byte = ((const uint8_t *)stream)[pos/8];
	return (byte >> (7 - (pos%8))) & 1;
}

uint32_t bit_advn(const void *stream, size_t *pos, size_t n) {
	const uint32_t bits = bit_getn(stream, *pos, n);
	*pos += n;
	return bits;
}

void bitstrm_seek(struct bitstrm *bs, size_t n) {
	const size_t max_peek = 33 + 32 + 8; // max peek in *_gamma_*()
	bs->pos += n;
	if (bs->len - bs->pos < max_peek) {
		bs->eof = bs->buf == bs->end;
		const size_t m = bs->len/8 - bs->pos/8;
		memmove(bs->end, bs->buf + bs->pos/8, m);
		memset(bs->end + m, 0, sizeof(bs->end) - m);
		bs->len = sizeof(bs->end)*8;
		bs->pos %= 8;
		bs->buf = bs->end;
	}
}


bool bitstrm_msb_next(struct bitstrm *bs) {
	const bool bit = bit_get(bs->buf, bs->pos);
	bitstrm_seek(bs, 1);
	return bit;
}

uint32_t bitstrm_msb_adv(struct bitstrm *bs, const size_t n) {
	const uint32_t bits = bit_getn(bs->buf, bs->pos, n);
	bitstrm_seek(bs, n);
	return bits;
}

uint32_t bitstrm_msb_peek_max25(const struct bitstrm *bs, const uint8_t n) {
	size_t i = bs->pos / 8;
	size_t o = bs->pos % 8;
	const uint32_t ret = buf_endian32b(bs->buf + i);
	return ret >> (32 - o - n) & bit_set32(n);
}

uint32_t bitstrm_msb_peek_high25(const struct bitstrm *bs) {
	size_t i = bs->pos / 8;
	size_t o = bs->pos % 8;
	return buf_endian32b(bs->buf + i) << o;
}

uint32_t bitstrm_msb_peek_32(const struct bitstrm *bs) {
	size_t i = bs->pos / 8;
	size_t o = bs->pos % 8;
	const uint32_t f = buf_endian32b(bs->buf + i);
	return f << o | (uint32_t)bs->buf[i+4] >> (8 - o);
}

uint32_t bitstrm_msb_adv_max25(struct bitstrm *bs, const uint8_t n) {
	const uint32_t bits = bitstrm_msb_peek_max25(bs, n);
	bitstrm_seek(bs, n);
	return bits;
}

uint32_t bitstrm_msb_gamma_zero(struct bitstrm *bs) {
	/* Gamma bit encoding (0 delimited):
		Coding  Range
		0       1
		10x     2-3
		110xx   4-7
		1110xxx 8-15
	 * and so on and so on. */
	const uint32_t bits = bitstrm_msb_peek_32(bs);
	const uint32_t z = bit_clo32(bits);
	bs->pos += z;
	const uint32_t val = (1u << 31) | bitstrm_msb_peek_32(bs);
	bitstrm_seek(bs, z+1);
	return val >> (31 - z);
}


bool bitstrm_lsb_next(struct bitstrm *bs) {
	const uint8_t byte = bs->buf[bs->pos/8];
	const bool bit = (byte >> (bs->pos%8)) & 1;
	bitstrm_seek(bs, 1);
	return bit;
}

static uint32_t bitstrm_lsb_peek_32_norev(const struct bitstrm *bs) {
	size_t i = bs->pos / 8;
	size_t o = bs->pos % 8;
	const uint32_t f = buf_endian32l(bs->buf + i);
	return (uint32_t)bs->buf[i+4] << 1 << (31 - o) | f >> o;
}

uint32_t bitstrm_lsb_peek_32(const struct bitstrm *bs) {
	return bit_rev32(bitstrm_lsb_peek_32_norev(bs));
}

static uint32_t lsb_code_adv(struct bitstrm *bs) {
	const uint32_t bits = bitstrm_lsb_peek_32_norev(bs);
	const uint32_t z = bit_ctz32(bits);
	bs->pos += z+1;
	return z;
}

static uint32_t lsb_code_val(struct bitstrm *bs, const uint32_t z) {
	const uint32_t val = bitstrm_lsb_peek_32_norev(bs);
	bitstrm_seek(bs, z);
	return val;
}

uint32_t bitstrm_lsb_gamma_one(struct bitstrm *bs) {
	/* LSB Gamma bit encoding (1 delimited):
		Range   Coding
		1            1
		2-3        x10
		4-7      xx100
		8-15   xxx1000
	 * There's a caveat... The encoded bits have to be reversed.
	*/
	const uint32_t z = lsb_code_adv(bs);
	const uint32_t val = lsb_code_val(bs, z);
	return 1u << z | bit_rev32(val) >> 1 >> (31 - z);
}

uint32_t bitstrm_lsb_exp_golomb(struct bitstrm *bs) {
	/* Like Gamma encoding, but starts at zero. */
	const uint32_t z = lsb_code_adv(bs);
	const uint32_t val = lsb_code_val(bs, z);
	const uint32_t mask = (1u << z) - 1;
	return (val & mask) + mask;
}


bool bitstrm_next(struct bitstrm *bs, const enum endianness e) {
	return e == big_endian
		? bitstrm_msb_next(bs)
		: bitstrm_lsb_next(bs);
}

uint32_t bitstrm_peek_32(const struct bitstrm *bs, const enum endianness e) {
	return e == big_endian
		? bitstrm_msb_peek_32(bs)
		: bitstrm_lsb_peek_32(bs);
}


void bitstrm_from_bytes(struct bitstrm *bs, const void *restrict mem,
const size_t len) {
	*bs = (struct bitstrm){
		.buf = mem,
		.pos = 0,
		.len = len*8,
	};
	bitstrm_seek(bs, 0);
}

void bitstrm_from_wuptr(struct bitstrm *bs, const struct wuptr data) {
	bitstrm_from_bytes(bs, data.ptr, data.len);
}
