// SPDX-License-Identifier: 0BSD
#include <string.h>

#include "misc/endian.h"

static uint16_t swap16(const uint16_t val) {
	return (uint16_t)(val << 8 | val >> 8);
}

static uint32_t swap32(const uint32_t val) {
	return (uint32_t)(val << 24
		| (val & 0x00ff00) << 8
		| (val & 0xff0000) >> 8
		| val >> 24);
}

static uint64_t swap64(const uint64_t val) {
	uint64_t ret = 0;
	for (size_t i = 0; i < sizeof(ret); ++i) {
		ret |= ((val >> i*8) & 0xff) << (56 - i*8);
	}
	return ret;
}

enum endianness which_end(void) {
	/* This is not UB after C99, except for traps representations, so it
	 * may be troublesome still, but there don't seem to be alternatives. */
	union {
		unsigned int ui;
		unsigned char uc[sizeof(unsigned int)];
	} test = {.ui = 1};
	return test.uc[0] ? little_endian : big_endian;
}

uint16_t endian16(const uint16_t val, const enum endianness e) {
	return e == which_end() ? val : swap16(val);
}

uint32_t endian32(const uint32_t val, const enum endianness e) {
	return e == which_end() ? val : swap32(val);
}

float endianf32(const uint32_t val, const enum endianness e) {
	const union int_real f = {.bytes = endian32(val, e)};
	return f.real;
}

uint16_t buf_endian16(const void *data, const enum endianness e) {
	const uint8_t *d = data;
	return (uint16_t)(e == big_endian
		? d[0] << 8 | d[1]
		: d[1] << 8 | d[0]);
}

uint32_t buf_endian24(const void *data, const enum endianness e) {
	const uint8_t *d = data;
	return (uint32_t)(e == big_endian
		? d[0] << 16 | d[1] << 8 | d[2]
		: d[2] << 16 | d[1] << 8 | d[0]);
}

uint32_t buf_endian32(const void *data, const enum endianness e) {
	const uint8_t *d = data;
	return (uint32_t)(e == big_endian
		? d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3]
		: d[3] << 24 | d[2] << 16 | d[1] << 8 | d[0]);
}

uint64_t buf_endian64(const void *data, const enum endianness e) {
	const uint8_t *d = data;
	uint64_t ret = 0;
	if (e == big_endian) {
		for (size_t i = 0; i < sizeof(ret); ++i) {
			ret |= (uint64_t)d[i] << ((64-8) - i*8);
		}
	} else {
		for (size_t i = 0; i < sizeof(ret); ++i) {
			ret |= (uint64_t)d[i] << (i*8);
		}
	}
	return ret;
}

float buf_endianf32(const void *data, const enum endianness e) {
	const union int_real f = {.bytes = buf_endian32(data, e)};
	return f.real;
}

void endian_loop16(uint16_t *data, const enum endianness e, const size_t n) {
	if (e != which_end()) {
		for (size_t i = 0; i < n; ++i) {
			data[i] = swap16(data[i]);
		}
	}
}

void endian_loop24(uint8_t *data, const enum endianness e, const size_t n) {
	if (e != which_end()) {
		for (size_t i = 0; i < n; ++i) {
			const uint8_t tmp = data[i*3];
			data[i*3] = data[i*3+2];
			data[i*3+2] = tmp;
		}
	}
}

void endian_loop32(uint32_t *data, const enum endianness e, const size_t n) {
	if (e != which_end()) {
		for (size_t i = 0; i < n; ++i) {
			data[i] = swap32(data[i]);
		}
	}
}

void endian_loop64(uint64_t *data, const enum endianness e, const size_t n) {
	if (e != which_end()) {
		for (size_t i = 0; i < n; ++i) {
			data[i] = swap64(data[i]);
		}
	}
}
