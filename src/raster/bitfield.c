// SPDX-License-Identifier: 0BSD
#include "misc/bit.h"
#include "misc/common.h"
#include "raster/bitfield.h"
#include "raster/fmt.h"

struct bf_key {
	uint8_t shr;
	uint8_t ones;
	uint8_t idx;
};

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

static void get_mul(struct bitfield *bf) {
	const uint32_t target = (uint32_t)(bf->outdepth > 8 ? USHRT_MAX : UCHAR_MAX)
		<< BITFIELD_SHIFT;
	for (uint8_t i = 0; i < bf->ch; ++i) {
		if (bf->comp[i].and) {
			bf->comp[i].mul = target / bf->comp[i].and + 1;
		}
	}
}
static struct bitfield init_bitfield(const uint8_t word_depth) {
	return (struct bitfield) {
		.word_size = word_depth/8,
	};
}

void bitfield_from_id(struct bitfield *bf, const enum bitfield_id id,
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
	const bool high_depth = maxdepth > 8;
	bf->outdepth = high_depth ? 16 : 8;
	bf->ch = z;
	bf->id = id;
	get_mul(bf);
}

static void key_swap(struct bf_key *restrict a, struct bf_key *restrict b) {
	if (a->shr < b->shr) {
		struct bf_key tmp = *a;
		*a = *b;
		*b = tmp;
	}
}

static void canon_form(struct bf_key canon[static 4], const struct bitfield *bf) {
	for (uint8_t z = 0; z < ARRAY_LEN(bf->comp); ++z) {
		if (z < bf->ch) {
			canon[z] = (struct bf_key) {
				.shr = (uint8_t)bf->comp[z].shr,
				.ones = (uint8_t)bit_cto32(bf->comp[z].and),
				.idx = z,
			};
		} else {
			canon[z] = (struct bf_key) {
				.idx = z,
			};
		}
	}
	key_swap(canon + 0, canon + 2);
	key_swap(canon + 1, canon + 3);
	key_swap(canon + 0, canon + 1);
	key_swap(canon + 2, canon + 3);
	key_swap(canon + 1, canon + 2);
}

static void get_id(struct bitfield *bf) {
	struct bf_key k[ARRAY_LEN(bf->comp)] = {0};
	canon_form(k, bf);

	enum bitfield_id id = 0;
	for (uint8_t z = 0; z < bf->ch; ++z) {
		id |= (unsigned)k[z].ones << (bf->ch - 1 - z)*4;
	}
	bf->id = id;
}

bool bitfield_from_mask(struct bitfield *bf, const uint32_t *mask,
const uint8_t ch, const uint8_t word_depth) {
	*bf = init_bitfield(word_depth);
	uint32_t xor_acc = 0;
	uint32_t maxdepth = 0;
	uint32_t totalbits = 0;
	for (uint8_t i = 0; i < ch; ++i) {
		const uint32_t zeroes = bit_ctz32(mask[i]);
		if (zeroes < sizeof(*mask)*8) {
			uint32_t rem = mask[i] >> zeroes;
			bf->comp[i].shr = zeroes;
			bf->comp[i].and = rem;

			const uint32_t ones = bit_cto32(rem);
			rem = rem >> 1 >> (ones - 1);

			// Ensure bits are contiguous and non-overlapping
			if (rem || (xor_acc & mask[i])) {
				return false;
			}
			xor_acc ^= mask[i];

			if (ones > maxdepth) {
				maxdepth = ones;
			}
			totalbits += ones;
		} else {
			bf->comp[i] = (struct bitfield_comp){0};
		}
	}
	if (!xor_acc || maxdepth >= word_depth / 2 || totalbits > word_depth) {
		return false;
	}

	const bool high_depth = maxdepth > 8;
	bf->outdepth = high_depth ? 16 : 8;
	bf->ch = ch;
	get_mul(bf);
	get_id(bf);
	return true;
}
