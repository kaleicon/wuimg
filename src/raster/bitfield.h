// SPDX-License-Identifier: 0BSD
#ifndef RASTER_BITFIELD
#define RASTER_BITFIELD

#include <stdint.h>

#include "misc/endian.h"
#include "raster/wuimg.h"

struct bitfield_comp {
	uint32_t shr, and, mul;
};

struct bitfield {
	uint8_t word_size;
	bool enable;
	enum endianness endian:8;
	struct bitfield_comp comp[4];
};

void bitfield_unpack(const struct bitfield *bf, struct wuimg *img,
const uint8_t *restrict src, align_t align);

size_t bitfield_unpack_from_file(const struct bitfield *bf, struct wuimg *img,
FILE *ifp);

bool bitfield_reduce(struct bitfield *bf, struct wuimg *img);

bool bitfield_load(struct bitfield *bf, struct wuimg *img,
const uint32_t *mask, uint8_t ch, uint8_t word_depth, enum endianness endian);

#endif /* RASTER_BITFIELD */
