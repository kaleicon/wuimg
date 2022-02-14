#include "bit.h"

uint_fast32_t bit_getn(const void *stream, const size_t pos, size_t n) {
	const uint_fast32_t mask = (1u << n) - 1;

	size_t o = pos / 8;
	size_t i = pos % 8;

	const uint8_t *s = (const uint8_t *)stream;
	n += i;
	int_fast32_t word = 0;
	while (n > 8) {
		word = word << 8 | s[o];
		++o;
		n -= 8;
	}
	word = word << 8 | s[o];
	return (uint_fast32_t)(word >> ((8 - n)%8)) & mask;
}

bool bit_get(const void *stream, const size_t pos) {
	const uint8_t byte = ((const uint8_t *)stream)[pos/8];
	return (byte >> (7 - (pos%8))) & 1;
}
