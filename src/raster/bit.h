#ifndef RASTER_BITSTREAM
#define RASTER_BITSTREAM

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct bitstrm {
	const uint8_t *buf;
	size_t pos;
	size_t len;
};

uint_fast32_t bit_getn(const void *stream, const size_t pos, size_t n);

bool bit_get(const void *stream, size_t pos);

uint_fast32_t bit_advn(const void *stream, size_t *pos, size_t n);

bool bit_adv(const void *stream, size_t *pos);


bool bitstrm_lsb_next(struct bitstrm *bs);

uint_fast32_t bitstrm_lsb_gamma(struct bitstrm *bs, bool delim);


struct bitstrm bitstrm_from_bytes(const void *mem, size_t bytes);

#endif /* RASTER_BITSTREAM */
