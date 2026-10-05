// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2023 kaleido
#ifndef RASTER_BITFIELD
#define RASTER_BITFIELD

#include <stdint.h>

#include "raster/pix.h"

static const uint32_t BITFIELD_SHIFT = 16;

// Values for extracting a bitfield component
struct bitfield_comp {
	uint32_t shr, and, mul;
};

struct bitfield {
	uint8_t word_size; // pixel size in bytes
	uint8_t outdepth; // output bitdepth
	uint8_t ch; // number of components
	uint16_t id; // packing identifier
	struct bitfield_comp comp[4];
};

/* Unpacks `w` elems from `src` into `dst`. Components are scaled to use the
 * whole output depth range. */
void bitfield_unpack(const struct bitfield *bf, void *restrict dst,
const void *restrict src, size_t w);

/* Sets bitfield according to `id` and `word_depth`.
 * For `id`, each hex digit, from least to most significant, specifies the bit
 * size of the next bitfield component, also in least to most order.
 * So 0x332 unpacks to a 2-bit component, then two 3-bit components.
 * `word_depth` is the size of each pixel in bits. Only 8, 16, 24, and 32 are
 * supported. */
void bitfield_from_id(struct bitfield *bf, uint16_t id, uint8_t word_depth);

/* Sets bitfield according to the `mask[ch]` array.
 * Each elem is a mask of contiguous bits that extracts one component of a
 * pixel. These must not overlap, and each must have a bit size less than
 * both 16 and `word_depth`.
 * Returns 0 in case of errors, otherwise a pix_layout describing the order
 * when unpacking, relative to `mask`. Calling `pix_layout_mul()` with this and
 * the image layout yields the true color order. */
enum pix_layout bitfield_from_mask(struct bitfield *bf, const uint32_t *mask,
uint8_t ch, uint8_t word_depth);

#endif /* RASTER_BITFIELD */
