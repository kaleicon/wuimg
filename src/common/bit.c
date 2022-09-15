#include "bit.h"

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

bool bit_adv(const void *stream, size_t *pos) {
	const bool bit = bit_get(stream, *pos);
	*pos += 1;
	return bit;
}

uint_fast32_t bit_adv_gamma(const void *stream, size_t *pos,
const bool delim) {
	size_t count = 0;
	while (bit_adv(stream, pos) != delim && count < 31) {
		++count;
	}
	return 1u << count | bit_advn(stream, pos, count);
}


static bool bit_lsb_get(const void *stream, const size_t pos) {
	const uint8_t byte = ((const uint8_t *)stream)[pos/8];
	return (byte >> (pos%8)) & 1;
}


bool bitstrm_lsb_next(struct bitstrm *bs) {
	const bool bit = bit_lsb_get(bs->buf, bs->pos);
	++bs->pos;
	return bit;
}

uint_fast32_t bitstrm_lsb_gamma(struct bitstrm *bs, const bool delim) {
	size_t count = 0;
	while (bitstrm_lsb_next(bs) != delim && count < 31) {
		++count;
	}
	uint_fast32_t word = 1;
	while (count) {
		word = (word << 1) | bitstrm_lsb_next(bs);
		--count;
	}
	return word;
}


struct bitstrm bitstrm_from_bytes(const void *mem, const size_t bytes) {
	return (struct bitstrm){.buf = mem, .pos = 0, .len = bytes*8};
}
