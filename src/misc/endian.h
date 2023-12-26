// SPDX-License-Identifier: 0BSD
#ifndef RASTER_ENDIAN
#define RASTER_ENDIAN

#include <stddef.h>
#include <stdint.h>

#define FOURCC(a, b, c, d) ((a << 24) | (b << 16) | (c << 8) | (d))

enum endianness {
	big_endian = 0,
	little_endian = 1,
};

union int_real {
        uint32_t bytes;
        float real;
};

enum endianness which_end(void);

uint16_t endian16(uint16_t val, enum endianness e);

uint32_t endian32(uint32_t val, enum endianness e);

float endianf32(uint32_t val, enum endianness e);


uint16_t buf_endian16(const void *data, enum endianness e);

uint32_t buf_endian24(const void *data, enum endianness e);

uint32_t buf_endian32(const void *data, enum endianness e);

uint64_t buf_endian64(const void *data, enum endianness e);

float buf_endianf32(const void *data, enum endianness e);

/* Swaps `n` data words in place if `e` doesn't match the processor's
 * endianness. Otherwise does nothing.
 * Happily enough, these also work the other way. If data is in native order,
 * after the call it will be in the specified endianness. So, to write an
 * array in big-endian order to a file, an unconditional call to
 * endian_loopN(data, big_endian, n) suffices. */
void endian_loop16(uint16_t *data, enum endianness e, size_t n);

void endian_loop24(uint8_t *data, enum endianness e, size_t n);

void endian_loop32(uint32_t *data, enum endianness e, size_t n);

void endian_loop64(uint64_t *data, enum endianness e, size_t n);

#endif /* RASTER_ENDIAN */
