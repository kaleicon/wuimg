// SPDX-License-Identifier: 0BSD
#ifndef RASTER_BITSTREAM
#define RASTER_BITSTREAM

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

uint32_t bit_set32(uint32_t bits);

struct bitstrm {
	const uint8_t *buf;
	size_t pos;
	size_t len;
};

uint32_t bit_getn(const void *stream, size_t pos, size_t n);

bool bit_get(const void *stream, size_t pos);

uint32_t bit_advn(const void *stream, size_t *pos, size_t n);


bool bitstrm_msb_next(struct bitstrm *bs);

uint32_t bitstrm_msb_adv(struct bitstrm *bs, size_t n);

uint32_t bitstrm_msb_gamma(struct bitstrm *bs, bool delim);

bool bitstrm_lsb_next(struct bitstrm *bs);

uint32_t bitstrm_lsb_gamma(struct bitstrm *bs, bool delim);


struct bitstrm bitstrm_from_bytes(const void *mem, size_t bytes);

#endif /* RASTER_BITSTREAM */
