// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef RASTER_ENDIAN
#define RASTER_ENDIAN

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FOURCC(a, b, c, d) ((a << 24) | (b << 16) | (c << 8) | (d))

enum endianness {
	big_endian = 0,
	little_endian = 1,
};

const char * endian_str(enum endianness e);

// Returns the machine endianness
static inline enum endianness which_end(void) {
	unsigned int u = 1;
	unsigned char c[sizeof(u)];
	memcpy(c, &u, sizeof(u));
	return c[0] ? little_endian : big_endian;
}

// Swap byte order in a word
static inline uint16_t swap16(const uint16_t val) {
	return (uint16_t)(val << 8 | val >> 8);
}
static inline uint32_t swap32(const uint32_t val) {
	return (uint32_t)(val << 24
		| (val & 0x00ff00) << 8
		| (val & 0xff0000) >> 8
		| val >> 24);
}
static inline uint64_t swap64(const uint64_t val) {
	uint64_t ret = 0;
	for (size_t i = 0; i < sizeof(ret); ++i) {
		ret |= ((val >> i*8) & 0xff) << (56 - i*8);
	}
	return ret;
}

/* All endian* functions work for reading and writing.
 * If `val` has endianness `e`, it will be returned in native order.
 * If `val` is in native order, it will be returned with endianness `e`.
*/
static inline uint16_t endian16(const uint16_t val, const enum endianness e) {
	return e == which_end() ? val : swap16(val);
}
static inline uint16_t endian16b(const uint16_t val) {
	return endian16(val, big_endian);
}
static inline uint16_t endian16l(const uint16_t val) {
	return endian16(val, little_endian);
}

static inline uint32_t endian32(const uint32_t val, const enum endianness e) {
	return e == which_end() ? val : swap32(val);
}
static inline uint32_t endian32b(const uint32_t val) {
	return endian32(val, big_endian);
}
static inline uint32_t endian32l(const uint32_t val) {
	return endian32(val, little_endian);
}

static inline float endianf32(uint32_t val, const enum endianness e) {
	val = endian32(val, e);
	float f;
	memcpy(&f, &val, sizeof(f));
	return f;
}
static inline float endianf32b(const uint32_t val) {
	return endianf32(val, big_endian);
}
static inline float endianf32l(const uint32_t val) {
	return endianf32(val, little_endian);
}

// Read data from unaligned buffers
static inline uint16_t buf_endian16b(const void *data) {
	const uint8_t *d = data;
	return (uint16_t)(d[0] << 8 | d[1]);
}
static inline uint16_t buf_endian16l(const void *data) {
	const uint8_t *d = data;
	return (uint16_t)(d[0] | d[1] << 8);
}
static inline uint16_t buf_endian16(const void *data, const enum endianness e) {
	return e == big_endian ? buf_endian16b(data) : buf_endian16l(data);
}

static inline uint32_t buf_endian24(const void *data, const enum endianness e) {
	const uint8_t *d = data;
	return (uint32_t)(e == big_endian
		? d[0] << 16 | d[1] << 8 | d[2]
		: d[2] << 16 | d[1] << 8 | d[0]);
}

static inline uint32_t buf_endian32b(const void *data) {
	const uint8_t *d = data;
	return (uint32_t)(d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3]);
}
static inline uint32_t buf_endian32l(const void *data) {
	const uint8_t *d = data;
	return (uint32_t)(d[0] | d[1] << 8 | d[2] << 16 | d[3] << 24);
}
static inline uint32_t buf_endian32(const void *data, const enum endianness e) {
	return (e == big_endian) ? buf_endian32b(data) : buf_endian32l(data);
}

static inline uint64_t buf_endian64(const void *data, const enum endianness e) {
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

static inline float buf_endianf32(const void *data, const enum endianness e) {
	uint32_t val = buf_endian32(data, e);
	float f;
	memcpy(&f, &val, sizeof(f));
	return f;
}
static inline float buf_endianf32b(const void *data) {
	return buf_endianf32(data, big_endian);
}
static inline float buf_endianf32l(const void *data) {
	return buf_endianf32(data, little_endian);
}

/* Swaps `n` data words in place if `e` doesn't match the processor's
 * endianness. */
void endian_loop16(uint16_t *data, enum endianness e, size_t n);

void endian_loop24(uint8_t *data, enum endianness e, size_t n);

void endian_loop32(uint32_t *data, enum endianness e, size_t n);

void endian_loop64(uint64_t *data, enum endianness e, size_t n);

#endif /* RASTER_ENDIAN */
