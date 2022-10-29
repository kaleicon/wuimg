#include "misc/endian.h"

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
	const enum endianness native = which_end();
	if (native != e) {
		return (uint16_t)(val << 8 | val >> 8);
	}
	return val;
}

uint32_t endian32(const uint32_t val, const enum endianness e) {
	const enum endianness native = which_end();
	if (native != e) {
		return (uint32_t)(val << 24
			| (val & 0x00ff00) << 8
			| (val & 0xff0000) >> 8
			| val >> 24);
	}
	return val;
}

static uint64_t endian64(const uint64_t val, const enum endianness e) {
	const enum endianness native = which_end();
	if (native != e) {
		uint64_t ret = 0;
		for (size_t i = 0; i < sizeof(ret); ++i) {
			ret |= ((val >> i*8) & 0xff) << (56 - i*8);
		}
		return ret;
	}
	return val;
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

uint32_t buf_endian32(const void *data, const enum endianness e) {
	const uint8_t *d = data;
	return (uint32_t)(e == big_endian
		? d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3]
		: d[3] << 24 | d[2] << 16 | d[1] << 8 | d[0]);
}

float buf_endianf32(const void *data, const enum endianness e) {
	const union int_real f = {.bytes = buf_endian32(data, e)};
	return f.real;
}

void endian_loop16(uint16_t *data, const enum endianness e, const size_t cnt) {
	if (e != which_end()) {
		for (size_t i = 0; i < cnt; ++i) {
			data[i] = endian16(data[i], e);
		}
	}
}

void endian_loop32(uint32_t *data, const enum endianness e, const size_t cnt) {
	if (e != which_end()) {
		for (size_t i = 0; i < cnt; ++i) {
			data[i] = endian32(data[i], e);
		}
	}
}

void endian_loop64(uint64_t *data, const enum endianness e, const size_t cnt) {
	if (e != which_end()) {
		for (size_t i = 0; i < cnt; ++i) {
			data[i] = endian64(data[i], e);
		}
	}
}
