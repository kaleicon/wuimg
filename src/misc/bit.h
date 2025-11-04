// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef RASTER_BITSTREAM
#define RASTER_BITSTREAM

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "misc/endian.h"
#include "misc/wustr.h"

uint8_t bit_min_wordsize_log2(unsigned bits);

uint8_t bit_min_wordsize_bits(unsigned bits);


uint16_t bit_rev16(uint16_t bits);

uint32_t bit_rev32(uint32_t bits);


uint32_t bit_ctz32(uint32_t bits);

uint32_t bit_cto32(uint32_t bits);

uint32_t bit_clz32(uint32_t bits);

uint32_t bit_clo32(uint32_t bits);

uint32_t bit_set32(uint32_t bits);


uint32_t bit_getn(const void *stream, size_t pos, size_t n);

bool bit_get(const void *stream, size_t pos);

uint32_t bit_advn(const void *stream, size_t *pos, size_t n);


struct bitstrm {
	uint8_t end[15];
	bool eof;
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

uint32_t bitstrm_lsb_peek_32(const struct bitstrm *bs);

uint32_t bitstrm_lsb_gamma_one(struct bitstrm *bs);

uint32_t bitstrm_lsb_exp_golomb(struct bitstrm *bs);


bool bitstrm_next(struct bitstrm *bs, enum endianness order);

uint32_t bitstrm_peek_32(const struct bitstrm *bs, enum endianness order);


void bitstrm_from_bytes(struct bitstrm *bs, const void *restrict mem,
size_t len);

void bitstrm_from_wuptr(struct bitstrm *bs, struct wuptr data);

#endif /* RASTER_BITSTREAM */
