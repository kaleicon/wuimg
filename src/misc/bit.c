#include "bit.h"

uint32_t bit_set32(const uint32_t bits) {
	const uint32_t ones = ~(uint32_t)0;
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


bool bitstrm_msb_next(struct bitstrm *bs) {
	const bool bit = bit_get(bs->buf, bs->pos);
	++bs->pos;
	return bit;
}

uint32_t bitstrm_msb_adv(struct bitstrm *bs, const size_t n) {
	return bit_advn(bs->buf, &bs->pos, n);
}

uint32_t bitstrm_msb_gamma(struct bitstrm *bs, const bool delim) {
	size_t count = 0;
	while (bitstrm_msb_next(bs) != delim && count < 31) {
		++count;
	}
	return 1u << count | bitstrm_msb_adv(bs, count);
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
	return (struct bitstrm){.buf = mem, .pos = 0, .len = bytes*8};
}
