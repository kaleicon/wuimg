#ifndef RASTER_ENDIAN
#define RASTER_ENDIAN

#include <stddef.h>
#include <stdint.h>

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

uint32_t buf_endian32(const void *data, enum endianness e);

float buf_endianf32(const void *data, enum endianness e);

void endian_loop16(uint16_t *data, enum endianness e, size_t cnt);

void endian_loop32(uint32_t *data, enum endianness e, size_t cnt);

void endian_loop64(uint64_t *data, enum endianness e, size_t cnt);

#endif /* RASTER_ENDIAN */
