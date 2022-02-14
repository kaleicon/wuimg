#ifndef RASTER_BITSTREAM
#define RASTER_BITSTREAM

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

uint_fast32_t bit_getn(const void *stream, const size_t pos, size_t n);

bool bit_get(const void *stream, size_t pos);

#endif /* RASTER_BITSTREAM */
