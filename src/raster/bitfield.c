// SPDX-License-Identifier: 0BSD
#include "misc/bit.h"
#include "misc/common.h"
#include "raster/bitfield.h"
#include "raster/fmt.h"

static const uint32_t BITFIELD_SHIFT = 16;

static uint32_t expand_bits(const uint32_t word, const struct bitfield_comp *c) {
	return (((word >> c->shr) & c->and) * c->mul) >> BITFIELD_SHIFT;
}

void bitfield_unpack(const struct bitfield *bf, struct wuimg *img,
const uint8_t *restrict src, const align_t align) {
	const uint8_t ch = img->channels;
	const uint8_t wd = bf->word_size - 2;

	const size_t instride = strip_length(img->w, bf->word_size*8, align);
	const size_t outstride = wuimg_stride(img);
	for (size_t y = 0; y < img->h; ++y) {
		uint8_t *d = img->data + y*outstride;
		const uint8_t *s = src + y*instride;
		for (size_t x = 0; x < img->w; ++x) {
			uint32_t word;
			switch (wd) {
			case 0:
				word = endian16(((uint16_t *)s)[x], bf->endian);
				break;
			case 1:
				word = buf_endian24(s + x*3, bf->endian);
				break;
			case 2:
				word = endian32(((uint32_t *)s)[x], bf->endian);
				break;
			}

			for (size_t z = 0; z < ch; ++z) {
				const size_t pos = x * ch + z;
				const uint32_t v = expand_bits(word, bf->comp + z);
				if (img->bitdepth == 16) {
					((uint16_t *)d)[pos] = (uint16_t)v;
				} else {
					d[pos] = (uint8_t)v;
				}
			}
		}
	}
}

size_t bitfield_unpack_from_file(const struct bitfield *bf, struct wuimg *img,
FILE *ifp) {
	if (!bf->enable) {
		return fmt_load_raster(img, ifp, bf->endian);
	}
	const size_t outsize = wuimg_size(img);
	const size_t insize = strip_length(img->w, bf->word_size*8, img->align_sh)
		* img->h;
	const bool newbuf = insize > outsize;
	uint8_t *src;
	if (newbuf) {
		src = malloc(insize);
		if (!src) {
			return 0;
		}
	} else {
		src = img->data + outsize - insize;
	}

	const size_t read = fread(src, 1, insize, ifp);
	bitfield_unpack(bf, img, src, img->align_sh);

	if (newbuf) {
		free(src);
	}
	return read;
}

struct bf_key {
	uint8_t shr;
	uint8_t ones;
	uint8_t idx;
};

static bool mod_img(struct bf_key *k, struct bitfield *bf, struct wuimg *img,
const uint8_t new_ch, const uint8_t bd, const enum pix_attr attr) {
	enum pix_layout l = pix_layout_pack(k[0].idx, k[1].idx, k[2].idx, k[3].idx);
	if (attr == pix_pack_1555) {
		l = pix_layout_mul(l, pix_bgra);
	} else if (bf->endian == little_endian) {
		l = pix_layout_mul(l, (img->channels < 4) ? pix_bgra : pix_abgr);
	}
	img->channels = new_ch;
	img->bitdepth = bd;
	img->attr = attr;
	img->layout = l;

	bf->enable = false;
	return true;
}

static bool key_comp(const struct bf_key *restrict a,
const struct bf_key *restrict b, const uint8_t nr) {
	for (uint8_t z = 0; z < nr; ++z) {
		if (a[z].shr != b[z].shr || a[z].ones != b[z].ones) {
			return false;
		}
	}
	return true;
}

static void key_swap(struct bf_key *restrict a, struct bf_key *restrict b) {
	if (a->shr < b->shr) {
		struct bf_key tmp = *a;
		*a = *b;
		*b = tmp;
	}
}

static void canon_form(struct bf_key canon[static 4], const struct bitfield *bf,
const uint8_t ch) {
	for (uint8_t z = 0; z < ARRAY_LEN(bf->comp); ++z) {
		if (z < ch) {
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

bool bitfield_reduce(struct bitfield *bf, struct wuimg *img) {
	const uint8_t ch = img->channels;

	struct bf_key canon[ARRAY_LEN(bf->comp)] = {0};
	canon_form(canon, bf, ch);

	const struct bf_key is_8888[4] = {
		{24, 8}, {16, 8}, {8, 8}, {0, 8},
	};
	const struct bf_key is_1555[4] = {
		{15, 1}, {10, 5}, {5, 5}, {0, 5},
	};
	switch (ch) {
	case 4:
		switch (bf->word_size) {
		case 4:
			if (key_comp(canon, is_8888, ch)) {
				return mod_img(canon, bf, img, 4, 8, 0);
			}
			break;
		case 2:
			if (key_comp(canon, is_1555, ch)) {
				return mod_img(canon, bf, img, 1, 16, pix_pack_1555);
			}
			const struct bf_key is_4444[4] = {
				{12, 4}, {8, 4}, {4, 4}, {0, 4},
			};
			if (key_comp(canon, is_4444, ch)) {
				return mod_img(canon, bf, img, 4, 4, 0);
			}
			break;
		}
		break;
	case 3:
		switch (bf->word_size) {
		case 3:
			if (key_comp(canon, is_8888 + 1, ch)) {
				return mod_img(canon, bf, img, 3, 8, 0);
			}
			break;
		case 2:
			if (key_comp(canon, is_1555 + 1, ch)) {
				return mod_img(canon, bf, img, 1, 16, pix_pack_1555);
			}
			break;
		}
		break;
	}
	return false;
}

bool bitfield_load(struct bitfield *bf, struct wuimg *img, const uint32_t *mask,
const uint8_t ch, const uint8_t word_depth, const enum endianness endian) {
	uint32_t xor_acc = 0;
	uint32_t maxdepth = 0;
	uint32_t totalbits = 0;
	const size_t siz = sizeof(*mask)*8;
	for (uint8_t i = 0; i < ch; ++i) {
		const uint32_t zeroes = bit_ctz32(mask[i]);
		if (zeroes < siz) {
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
	if (!xor_acc || maxdepth > word_depth / 2 || totalbits > word_depth) {
		return false;
	}

	const bool high_depth = maxdepth > 8;
	const uint32_t target = (uint32_t)(high_depth ? USHRT_MAX : UCHAR_MAX)
		<< BITFIELD_SHIFT;
	for (uint8_t i = 0; i < ch; ++i) {
		if (bf->comp[i].and) {
			bf->comp[i].mul = target / bf->comp[i].and + 1;
		}
	}

	bf->word_size = word_depth/8;
	bf->enable = true;
	bf->endian = endian;
	img->channels = ch;
	img->bitdepth = high_depth ? 16 : 8;
	return true;
}
