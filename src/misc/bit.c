// SPDX-License-Identifier: 0BSD
#include <string.h>

#include "bit.h"
#include "endian.h"

uint32_t bit_clz32(uint32_t bits) {
	const uint8_t mul[] = {
		31, 22, 30, 21, 18, 10, 29, 2, 20, 17, 15, 13, 9, 6, 28, 1,
		23, 19, 11, 3, 16, 14, 7, 24, 12, 4, 8, 25, 5, 26, 27, 0
	};
	bits |= bits >> 1;
	bits |= bits >> 2;
	bits |= bits >> 4;
	bits |= bits >> 8;
	bits |= bits >> 16;
	return mul[ (bits * 0x07c4acdd) >> 27 ];
}


uint32_t bit_set32(const uint32_t bits) {
	const uint32_t ones = ~0u;
	return ones >> (sizeof(ones)*8 - bits);
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
	const size_t max_peek = 32 + 32 + 8; // max peek in msb_gamma()
	bs->pos += n;
	if (bs->pos + max_peek >= bs->len) {
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

uint32_t bitstrm_msb_peek_max25(const struct bitstrm *bs, uint8_t n) {
	size_t i = bs->pos / 8;
	size_t o = bs->pos % 8;
	const uint32_t ret = buf_endian32(bs->buf + i, big_endian);
	return ret >> (32 - o - n) & bit_set32(n);
}

uint32_t bitstrm_msb_peek_high25(const struct bitstrm *bs) {
	size_t i = bs->pos / 8;
	size_t o = bs->pos % 8;
	return buf_endian32(bs->buf + i, big_endian) << o;
}

uint32_t bitstrm_msb_peek_32(const struct bitstrm *bs) {
	size_t i = bs->pos / 8;
	size_t o = bs->pos % 8;
	const uint32_t f = buf_endian32(bs->buf + i, big_endian);
	return f << o | (uint32_t)bs->buf[i+4] >> (8 - o);
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
	const uint32_t z = bit_clz32(~bits);
	bs->pos += z;
	// Top bit will be 0
	const uint32_t val = (1u << 31) | bitstrm_msb_peek_32(bs);
	bitstrm_seek(bs, z+1);
	return val >> (31 - z);
}


bool bitstrm_lsb_next(struct bitstrm *bs) {
	const uint8_t byte = bs->buf[bs->pos/8];
	const bool bit = (byte >> (bs->pos%8)) & 1;
	++bs->pos;
	return bit;
}

uint32_t bitstrm_lsb_gamma(struct bitstrm *bs, const bool delim) {
	size_t count = 0;
	while (bitstrm_lsb_next(bs) != delim && count < 31) {
		++count;
	}
	uint32_t word = 1;
	while (count) {
		word = (word << 1) | bitstrm_lsb_next(bs);
		--count;
	}
	return word;
}


struct bitstrm bitstrm_from_bytes(const void *mem, const size_t bytes) {
	struct bitstrm bs = (struct bitstrm){
		.buf = mem,
		.pos = 0,
		.len = bytes*8,
	};
	bitstrm_seek(&bs, 0);
	return bs;
}
