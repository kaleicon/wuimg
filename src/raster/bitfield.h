// SPDX-License-Identifier: 0BSD
#ifndef RASTER_BITFIELD
#define RASTER_BITFIELD

#include <stdint.h>

#include "raster/pix.h"

enum bitfield_id {
	bitfield_id_1555 = 0x1555,
	bitfield_id_332 = 0x332,
};

struct bitfield_comp {
	uint32_t shr, and, mul;
};

struct bitfield {
	uint8_t word_size;
	uint8_t outdepth;
	uint8_t ch;
	uint16_t id;
	struct bitfield_comp comp[4];
};

void bitfield_unpack(const struct bitfield *bf, void *restrict dst,
const void *restrict src, size_t w);

void bitfield_from_id(struct bitfield *bf, uint16_t id, uint8_t word_depth);

enum pix_layout bitfield_from_mask(struct bitfield *bf, const uint32_t *mask,
uint8_t ch, uint8_t word_depth);

#endif /* RASTER_BITFIELD */
