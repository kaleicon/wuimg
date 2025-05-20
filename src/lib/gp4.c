// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "lib/gp4.h"
#include "misc/bit.h"
#include "misc/endian.h"
#include "misc/mem.h"

static const uint8_t BAND_W = sizeof(uint16_t);
static const uint8_t GP4_COLORS = 16;
#define LUT_H (GP4_COLORS + 1)
#define LUT_W GP4_COLORS

static size_t get_rle_count(struct bitstrm *bs) {
	/* Row count encoding:
		0x         x + 2
		10xx       xx + 4
		110xxx     xxx + 8
		111xxxxxx  xxxxxx + 16
	 * If xxxxxx + 16 == 79 (xxxxxx is all ones), read 10 bits
	 * and return those + 79.
	*/

	uint32_t bits = bitstrm_msb_peek_high25(bs);
	uint32_t ones = bit_clo32(bits);
	uint32_t skip, keep, add;
	if (ones < 3) {
		++ones;
		skip = ones;
		keep = ones;
		add = 1u << ones;
	} else if (ones < 9) {
		skip = 3;
		keep = 6;
		add = 16;
	} else {
		skip = 9;
		keep = 10;
		add = 79;
	}
	uint32_t count = (bits << skip >> (32 - keep)) + add;
	bitstrm_seek(bs, skip + keep);
	return count;
}

static void get_rle_offset(struct bitstrm *bs, size_t *restrict xoff,
size_t *restrict yoff, uint32_t bits, uint32_t b_seek) {
	/* Offset coding:
		1)  0xxxx         horz:1, vert:xxxx
		2)  10xxx         horz:0, vert:xxx
		3)  111... 0xxxx  horz:nb of ones, vert:xxxx

	 * vert has a bias of 8 (i.e. 0 means -8) except for two values in
	 * case 2: if xxx is 0, vert is -16; if xxx is 1, vert is -8.
	 * horz is negative.
	*/

	uint32_t ones = bit_clo32(bits);
	uint32_t sub = 8;
	*xoff = ones;
	if (ones <= 1) {
		bitstrm_seek(bs, b_seek + 5);
		const uint32_t off = (bits >> 27) & 0xf;
		sub <<= ones & (off == 0);
		sub |= ones & (off == 1);
		*xoff ^= 1;
		*yoff = off;
	} else {
		ones += b_seek;
		bitstrm_seek(bs, ones);
		while (ones == 32) {
			ones = bit_clo32(bitstrm_msb_peek_32(bs));
			*xoff += ones;
			bitstrm_seek(bs, ones);
		}
		*yoff = bitstrm_msb_adv(bs, 4 + 1 /* harmless top zero */);
	}
	*yoff -= sub;
}

static uint8_t cache_retrieve_pair(uint8_t *restrict lut, const uint8_t ly,
struct bitstrm *bs, uint8_t *restrict dst, uint32_t bits, uint32_t b_seek) {
	bits = ~bits;
	uint32_t i = bit_clz32(bits) & 0xf;
	bits = bits << (i + 1);
	uint32_t k = bit_clz32(bits) & 0xf;
	bitstrm_seek(bs, i + k + 2 + b_seek);

	uint8_t a = memcycle(lut + ly*LUT_W, i);
	uint8_t b = memcycle(lut + a*LUT_W, k);
	*dst = a << 4 | b;
	return b;
}

static uint8_t cache_retrieve_row(uint8_t *restrict lut, uint8_t ly,
struct bitstrm *bs, uint8_t *restrict dst, uint32_t leftover_bits,
uint32_t b_seek) {
	/* Cache procedure:
	 *   1. When writing to a new band, initialize the row number to 16.
	 *      Otherwise, use the previous color read from cache as row index.
	 *   2. Count the number of 1 bits in the bitstream. This is the
	 *      byte index into the row.
	 *      (What if there are more than 15 bits set? No clue.)
	 *   3. Move the selected color to the front of the row, write it to
	 *      the output, and save it for the next cache access.
	 *   4. Repeat until 4 pixels are written. */

	/* We retrieve colors in pairs to reduce bitstrm calls, and to easily
	 * pack two colors into each byte. */
	ly = cache_retrieve_pair(lut, ly, bs, dst, leftover_bits, b_seek);
	return cache_retrieve_pair(lut, ly, bs, dst + 1,
		bitstrm_msb_peek_32(bs), 0);
}

size_t gp4_decode(const struct gp4_desc *desc, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}

	uint8_t lut[LUT_H][LUT_W];
	for (uint8_t ly = 0; ly < LUT_H; ++ly) {
		for (uint8_t lx = 0; lx < LUT_W; ++lx) {
			lut[ly][lx] = (ly + lx) & 0x0f;
		}
	}

	struct mparser mp = desc->mp;
	struct bitstrm bs = bitstrm_from_wuptr(mp_remaining(&mp));

	const size_t stride = wuimg_stride(img);
	const size_t bands = stride / BAND_W;

	uint8_t *dst = img->data;
	size_t band = 0;
	size_t y = 0;
	while (band < bands) {
		uint8_t ly = LUT_H - 1;
		y = 0;
		while (y < img->h) {
			uint32_t bits = bitstrm_msb_peek_32(&bs);
			const uint32_t b_skip = 1;
			const bool is_rle = bits >> (32 - b_skip);
			bits <<= b_skip;
			uint8_t *d = dst + stride*y + BAND_W*band;
			if (is_rle) {
				/* We use unsigned offsets even though these
				 * may be negative quantities.
				 * This avoids some messy casting dances and
				 * simplifies range checks; too high or too low
				 * values will exceed the image dimensions all
				 * the same.
				 * The penalty is that this only works on two's
				 * complement machines, and demands this long
				 * explanation. */
				size_t xoff, yoff;
				get_rle_offset(&bs, &xoff, &yoff, bits, b_skip);

				xoff = band - xoff;
				yoff = y + yoff;
				const size_t count = get_rle_count(&bs);
				if (xoff >= bands || yoff >= img->h
				|| count > img->h - yoff || count > img->h - y) {
					goto loop_escape;
				}

				for (size_t i = 0; i < count; ++i) {
					memcpy(d + stride*i,
						dst + stride*(yoff+i) + BAND_W*xoff,
						BAND_W);
				}
				y += count;
			} else {
				ly = cache_retrieve_row(*lut, ly, &bs, d, bits,
					b_skip);
				++y;
			}
		}
		++band;
	}
loop_escape:
	return band*img->h + y;
}

enum wu_error gp4_parse(struct gp4_desc *desc, const struct wuptr mem,
struct wuimg *img) {
	/* GP4 structure:
		Offset  Type    Name
		0       u16     X
		2       u16     Y
		4       u16     Width
		6       u16     Height
		8       u16     Palette[16]
		40

	 * Palette bitfield, MSB to LSB:
		+----+-+----+-+----+--+
		|gggg|x|rrrr|x|bbbb|xx|
		+----+-+----+-+----+--+
		 fedc b a987 6 5432 10

	 * Width and Height have a bias of -1.
	*/

	desc->mp = mp_wuptr(mem);
	const uint8_t *header = mp_slice(&desc->mp, 40);
	if (!header) {
		return wu_unexpected_eof;
	}

	desc->x = buf_endian16(header, big_endian);
	desc->y = buf_endian16(header + 2, big_endian);
	img->w = buf_endian16(header + 4, big_endian) + 1;
	img->h = buf_endian16(header + 6, big_endian) + 1;
	img->channels = 1;
	img->bitdepth = 4;
	img->layout = pix_grba;
	wuimg_align(img, BAND_W); // Make the last band the same size as the others
	struct palette *pal = wuimg_palette_init(img);
	if (!pal) {
		return wu_alloc_error;
	}

	for (uint8_t c = 0; c < GP4_COLORS; ++c) {
		const uint16_t color = buf_endian16(header + 8 + c*2, big_endian);
		uint8_t *dst = (uint8_t *)(pal->color + c);
		for (uint8_t z = 0; z < 3; ++z) {
			const int shr = 12 - z*5;
			dst[z] = (uint8_t)(((color >> shr) & 0x0f) * 0x11);
		}
		dst[3] = 0xff;
	}
	return wuimg_verify(img);
}
