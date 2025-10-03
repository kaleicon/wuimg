// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef RASTER_ENDIAN
#define RASTER_ENDIAN

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FOURCC(a, b, c, d) ((a << 24) | (b << 16) | (c << 8) | (d))

enum endianness {
	big_endian = 0,
	little_endian = 1,
};

const char * endian_str(enum endianness e);

enum endianness which_end(void);


/* All endian* functions work for reading and writing.
 * If `val` has endianness `e`, it will be returned in native order.
 * If `val` is in native order, it will be returned with endianness `e`.
*/
uint16_t endian16(uint16_t val, enum endianness e);
uint16_t endian16b(uint16_t val);
uint16_t endian16l(uint16_t val);

uint32_t endian32(uint32_t val, enum endianness e);
uint32_t endian32b(uint32_t val);
uint32_t endian32l(uint32_t val);

float endianf32(uint32_t val, enum endianness e);
float endianf32b(uint32_t val);
float endianf32l(uint32_t val);

/* Read data from unaligned buffers. */
uint16_t buf_endian16b(const void *data);
uint16_t buf_endian16l(const void *data);
uint16_t buf_endian16(const void *data, enum endianness e);

uint32_t buf_endian24(const void *data, enum endianness e);

uint32_t buf_endian32b(const void *data);
uint32_t buf_endian32l(const void *data);
uint32_t buf_endian32(const void *data, enum endianness e);

uint64_t buf_endian64(const void *data, enum endianness e);

float buf_endianf32(const void *data, enum endianness e);
float buf_endianf32b(const void *data);
float buf_endianf32l(const void *data);

/* Swaps `n` data words in place if `e` doesn't match the processor's
 * endianness. */
void endian_loop16(uint16_t *data, enum endianness e, size_t n);

void endian_loop24(uint8_t *data, enum endianness e, size_t n);

void endian_loop32(uint32_t *data, enum endianness e, size_t n);

void endian_loop64(uint64_t *data, enum endianness e, size_t n);

#endif /* RASTER_ENDIAN */
