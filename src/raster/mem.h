#ifndef RASTER_MEM
#define RASTER_MEM

#include <stdint.h>

uint8_t memcycle(void *dst, size_t len);

void memtessel(void *restrict dst, const void *restrict src, size_t size,
size_t bytes);

void memrepeat(void *dst, size_t pos, size_t offset, size_t count);

void memrepeat_or_zero(void *dst, size_t pos, size_t offset, size_t count);

#endif /* RASTER_MEM */
