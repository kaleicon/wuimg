// SPDX-License-Identifier: 0BSD
#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "raster/bitfield.h"
#include "raster/fmt.h"

static const uint32_t BITFIELD_SHIFT = 16;

static uint32_t expand_bits(const uint32_t word, const struct bitfield_comp *c) {
	return (((word >> c->shr) & c->and) * c->mul) >> BITFIELD_SHIFT;
}

void bitfield_unpack(const struct bitfield *bf, void *restrict dst,
const void *restrict src, const size_t w) {
	const uint8_t wd = bf->word_size - 1;
	for (size_t x = 0; x < w; ++x) {
		uint32_t word = 0;
		switch (wd) {
		case 0:
			word = ((const uint8_t *)src)[x];
			break;
		case 1:
			word = ((const uint16_t *)src)[x];
			break;
		case 2:
			memcpy(&word, (const uint8_t *)src + x*3, 3);
			word >>= (which_end() == big_endian) ? 8 : 0;
			break;
		case 3:
			word = ((const uint32_t *)src)[x];
			break;
		}

		for (uint8_t z = 0; z < bf->ch; ++z) {
			const size_t pos = x * bf->ch + z;
			const uint32_t v = expand_bits(word, bf->comp + z);
			if (bf->outdepth == 16) {
				((uint16_t *)dst)[pos] = (uint16_t)v;
			} else {
				((uint8_t *)dst)[pos] = (uint8_t)v;
			}
		}
	}
}

static uint32_t get_out_scale(const struct bitfield *bf) {
	return (uint32_t)(bf->outdepth > 8 ? USHRT_MAX : UCHAR_MAX)
		<< BITFIELD_SHIFT;
}

static struct bitfield init_bitfield(const uint8_t word_depth) {
	return (struct bitfield) {
		.word_size = word_depth/8,
	};
}

void bitfield_from_id(struct bitfield *bf, const uint16_t id,
const uint8_t word_depth) {
	*bf = init_bitfield(word_depth);
	uint8_t z = 0;
	uint8_t pos = 0;
	uint8_t maxdepth = 0;
	while (z < 4) {
		const uint8_t ones = (id >> z*4) & 0xf;
		if (!ones) {
			break;
		}
		bf->comp[z].shr = pos;
		bf->comp[z].and = bit_set32(ones);

		if (ones > maxdepth) {
			maxdepth = ones;
		}
		pos += ones;
		++z;
	}
	bf->outdepth = maxdepth > 8 ? 16 : 8;
	bf->ch = z;
	bf->id = id;
	const uint32_t target = get_out_scale(bf);
	for (uint8_t i = 0; i < z; ++i) {
		bf->comp[i].mul = target / bf->comp[i].and + 1;
	}
}

struct bf_key {
	uint8_t idx;
	uint8_t zeroes;
	uint8_t ones;
};

static void key_swap(struct bf_key *restrict a, struct bf_key *restrict b) {
	if (a->zeroes > b->zeroes) {
		struct bf_key tmp = *a;
		*a = *b;
		*b = tmp;
	}
}

static enum pix_layout sort_masks(struct bf_key *k) {
	key_swap(k + 0, k + 2);
	key_swap(k + 1, k + 3);
	key_swap(k + 0, k + 1);
	key_swap(k + 2, k + 3);
	key_swap(k + 1, k + 2);
	return 0u << k[0].idx*2 | 1u << k[1].idx*2
		| 2u << k[2].idx*2 | 3u << k[3].idx*2;
}

enum pix_layout bitfield_from_mask(struct bitfield *bf, const uint32_t *mask,
const uint8_t ch, const uint8_t word_depth) {
	/* Masks are used by BMP and XWD, where they must meet the following
	 * requirements:
	 * - Bits of each mask must be contiguous
	 * - Masks must not overlap
	 * It's likely the OR of all masks should also be contiguous, but we
	 * don't check that at the moment. A mask with no bits, on the other
	 * hand, is fine (i.e. for Alpha).
	 * As for ourselves, we want easy legibility for optimization purposes
	 * (i.e. OpenGL). That means we need a bitfield ID and for the image
	 * pix_layout to reflect the stored order, as when `bitfield_from_id`
	 * is called. These values fall out naturally when masks are sorted by
	 * number of trailing zeroes.
	*/
	*bf = init_bitfield(word_depth);
	struct bf_key k[ARRAY_LEN(bf->comp)];

	const uint32_t max = sizeof(*mask)*8;
	uint32_t totalbits = 0;
	uint32_t maxdepth = 0;
	uint32_t acc = 0;
	for (uint8_t i = 0; i < ch; ++i) {
		uint32_t m = mask[i];
		const uint32_t zeroes = bit_ctz32(m);
		m >>= zeroes;
		const uint32_t ones = bit_cto32(m);
		if (ones) {
			m = m >> 1 >> (ones - 1);
			// Ensure bits are contiguous and non-overlapping
			if (m || (acc & mask[i])) {
				return 0;
			}
			acc ^= mask[i];
			totalbits += ones;
			maxdepth = u32max(maxdepth, ones);
		}
		k[i] = (struct bf_key) {
			.idx = i,
			.zeroes = (uint8_t)zeroes,
			.ones = (uint8_t)ones,
		};
	}
	for (uint8_t i = ch; i < ARRAY_LEN(bf->comp); ++i) {
		k[i] = (struct bf_key) {
			.idx = i,
			.zeroes = (uint8_t)max,
		};
	}
	if (!acc || totalbits > word_depth || maxdepth > u32min(word_depth, 16)) {
		return false;
	}

	const enum pix_layout layout = sort_masks(k);
	bf->outdepth = maxdepth > 8 ? 16 : 8;
	bf->ch = ch;
	const uint32_t target = get_out_scale(bf);
	for (uint8_t i = 0; i < ch; ++i) {
		if (k[i].zeroes >= max) {
			break;
		}
		const uint32_t and = bit_set32(k[i].ones);
		bf->comp[i] = (struct bitfield_comp) {
			.shr = k[i].zeroes,
			.and = and,
			.mul = target / and + 1,
		};
		bf->id |= k[i].ones << (i*4);
	}
	return layout;
}
