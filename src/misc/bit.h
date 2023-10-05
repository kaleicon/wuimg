// SPDX-License-Identifier: 0BSD
#ifndef RASTER_BITSTREAM
#define RASTER_BITSTREAM

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

uint32_t bit_clz32(uint32_t bits);

uint32_t bit_set32(uint32_t bits);


uint32_t bit_getn(const void *stream, size_t pos, size_t n);

bool bit_get(const void *stream, size_t pos);

uint32_t bit_advn(const void *stream, size_t *pos, size_t n);


struct bitstrm {
	uint8_t end[16];
	const uint8_t *buf;
	size_t pos;
	size_t len;
};

void bitstrm_seek(struct bitstrm *bs, size_t n);


bool bitstrm_msb_next(struct bitstrm *bs);

uint32_t bitstrm_msb_adv(struct bitstrm *bs, size_t n);

uint32_t bitstrm_msb_peek_max25(const struct bitstrm *bs, uint8_t n);

uint32_t bitstrm_msb_peek_high25(const struct bitstrm *bs);

uint32_t bitstrm_msb_peek_32(const struct bitstrm *bs);

uint32_t bitstrm_msb_adv_max25(struct bitstrm *bs, uint8_t n);

uint32_t bitstrm_msb_gamma_zero(struct bitstrm *bs);


bool bitstrm_lsb_next(struct bitstrm *bs);

uint32_t bitstrm_lsb_gamma_one(struct bitstrm *bs);


struct bitstrm bitstrm_from_bytes(const void *mem, size_t bytes);

#endif /* RASTER_BITSTREAM */
