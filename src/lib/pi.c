// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "misc/mparser.h"
#include "pi.h"

/* Documented in
https://mooncore.eu/bunny/txt/pi-pic.htm

 * Spec (in japanese)
https://mooncore.eu/bunny/txt/pitech.txt

 * DPC and .g/.lsp:
https://gitlab.com/bunnylin/supersakura/-/blob/dev/doc/gfx/cgl-dpc-p-g.md
*/

// Enable to use slightly slower but clearly correct code.
static const bool EXACT_BITS = false;
/* Assume a .g/.lsp file if signature doesn't match, ignoring actual extension.
 * This ensures the fuzzer tests the lsp path. */
static const bool PI_ACCEPT_ANY_EXT = true;

enum pi_repeat_src {
	pi_last4 = 0,
	pi_1row = 1,
	pi_2row = 2,
	pi_1row_next = 6,
	pi_1row_prev = 7,
};

static uint8_t table_lookup(uint8_t *table, const unsigned depth,
const size_t y, const size_t x) {
	return memcycle(table + y*depth, x);
}

static void init_delta_table(uint8_t *table, const size_t colors) {
	for (size_t y = 0; y < colors; ++y) {
		for (size_t x = 0; x < colors; ++x) {
			table[colors*y + x] = (uint8_t)(
				(colors + y - x) & (colors - 1)
			);
		}
	}
}

static size_t exec_repeat(uint8_t *restrict output, size_t i,
const enum pi_repeat_src loc, size_t cnt, size_t diff) {
	switch (loc) {
	case pi_last4:
		if (output[i-2] == output[i-1] || i == 2) {
			diff = 2;
		} else {
			diff = 4;
		}
		goto end_repeat;
	case pi_1row:
		break;
	case pi_2row:
		diff *= 2;
		break;
	case pi_1row_next:
		diff -= 1;
		break;
	case pi_1row_prev:
		diff += 1;
		break;
	}

	if (i < diff) {
		const bool init = diff & 1;
		const uint8_t pair[2] = {
			output[init],
			output[init ^ 1],
		};
		/* Do ceil division because `i` is a multiple of two, `diff`
		 * may be odd, and we need `i` to be greater than `diff` */
		const size_t m = zumin(cnt, (diff - i + 1)/2);
		memset16((uint16_t *)(output + i), pair, m);
		i += m*2;
		cnt -= m;
	}
end_repeat:
	memrepeat(output, i, diff, cnt*2);
	return i + cnt*2;
}

static enum pi_repeat_src read_repeat_loc(struct bitstrm *bs) {
	/* Location codes: 00, 01, 10, 110, 111 */
	const uint32_t bits = bitstrm_msb_peek_max25(bs, 3);
	const uint8_t diff = (bits > 5) ? 3 : 2;
	bitstrm_seek(bs, diff);
	return (enum pi_repeat_src)(bits >> (3 - diff));
}

static uint32_t read_8bit_delta_exact(struct bitstrm *bs) {
	/* 8-bit delta encoding:
		Code            Values
		1x              0-1
		00x             1-2
		010xx           4-7
		0110xxx         8-15
		01110xxxx       16-31
		011110xxxxx     32-63
		0111110xxxxxx   64-127
		0111111xxxxxxx  128-255
	*/
	if (bitstrm_msb_next(bs)) {
		return bitstrm_msb_next(bs);
	} else { // 00
		uint32_t sh = 0;
		// 010
		if (bitstrm_msb_next(bs)) { // Weee
			// 0110
			if (bitstrm_msb_next(bs)) { // eeee
				// 01110
				if (bitstrm_msb_next(bs)) { // eeee
					// 011110
					if (bitstrm_msb_next(bs)) { // eeee
						// 0111110
						if (bitstrm_msb_next(bs)) { // eeee
							// 0111111
							if (bitstrm_msb_next(bs)) {
								++sh;
							}
							++sh;
						}
						++sh;
					}
					++sh;
				}
				++sh;
			}
			++sh;
		}
		++sh;
		return bitstrm_msb_adv(bs, sh) | (1U << sh);
	}
}

static uint32_t read_8bit_delta_word(uint32_t word, uint32_t *dt) {
	uint32_t read, xor;
	if (word >= 0x01U << (32 - 1)) {        //       1x
		read = 2; xor = 0x01 << 1;
	} else if (word >= 0x3fU << (32 - 7)) { // 0111111xxxxxxx
		read = 14; xor = 0x1f << 8;
	} else if (word >= 0x1fU << (32 - 6)) { // 0111110xxxxxx
		read = 13; xor = 0x3f << 6;
	} else if (word >= 0x0fU << (32 - 5)) { //  011110xxxxx
		read = 11; xor = 0x1f << 5;
	} else if (word >= 0x07U << (32 - 4)) { //   01110xxxx
		read = 9; xor = 0x0f << 4;
	} else if (word >= 0x03U << (32 - 3)) { //    0110xxx
		read = 7; xor = 0x07 << 3;
	} else if (word >= 0x01U << (32 - 2)) { //     010xx--
		read = 5; xor = 0x03 << 2;
	} else {                                //      00x----
		read = 3; xor = 0x01 << 1;
	}
	*dt = (word >> (32 - read)) ^ xor;
	return read;
}

static uint32_t read_4bit_delta_exact(struct bitstrm *bs) {
	/* 4-bit delta encoding:
		Code    Values
		1x      0-1
		00x     2-3
		010xx   4-7
		011xxx  8-15
	*/
	if (bitstrm_msb_next(bs)) {
		return bitstrm_msb_next(bs);
	}
	unsigned int sh = 0;
	if (bitstrm_msb_next(bs)) {
		if (bitstrm_msb_next(bs)) {
			++sh;
		}
		++sh;
	}
	++sh;
	return bitstrm_msb_adv(bs, sh) | (1U << sh);
}

static uint32_t read_4bit_delta_word(uint32_t word, uint32_t *dt) {
	uint32_t read, xor;
	switch (word >> 29) {
	case 0: case 1:
		read = 3; xor = 0x02;
		break;
	case 2:
		read = 5; xor = 0x0c;
		break;
	case 3:
		read = 6; xor = 0x10;
		break;
	default:
		read = 2; xor = 0x02;
		break;
	}
	*dt = (word >> (32 - read)) ^ xor;
	return read;
}

static void read_delta_codes(struct bitstrm *bs, uint32_t *dt,
const uint8_t codes, const unsigned depth) {
	if (EXACT_BITS) {
		for (size_t i = 0; i < codes; ++i) {
			dt[i] = (depth == 1 << 4)
				? read_4bit_delta_exact(bs)
				: read_8bit_delta_exact(bs);
		}
	} else {
		// For two codes, we need at most 28 bits
		const uint32_t word = bitstrm_msb_peek_32(bs);
		uint32_t read = 0;
		for (size_t i = 0; i < codes; ++i) {
			read += (depth == 1 << 4)
				? read_4bit_delta_word(word << read, dt + i)
				: read_8bit_delta_word(word << read, dt + i);
		}
		bitstrm_seek(bs, read);
	}
}

static size_t bt_decode_loop(uint8_t *restrict output, const size_t dims,
struct bitstrm *bs, const size_t width, uint8_t *restrict table,
const unsigned depth) {
	size_t i = 0;
	uint8_t prev = 0;
	while (i < dims - 1 && !bs->eof) {
		uint32_t dt[2];
		read_delta_codes(bs, dt, ARRAY_LEN(dt), depth);
		output[i] = table_lookup(table, depth, prev, dt[0]);
		output[i+1] = table_lookup(table, depth, output[i], dt[1]);
		i += 2;

		if (i >= dims) {
			break;
		} else if (i == 2 || !bitstrm_msb_next(bs)) {
			enum pi_repeat_src loc[2];
			loc[1] = depth; // sentinel value
			for (unsigned cur = 0;; cur = !cur) {
				loc[cur] = read_repeat_loc(bs);
				if (loc[cur] == loc[!cur]) {
					break;
				}
				size_t cnt = bitstrm_msb_gamma_zero(bs)
					- (i == 2);
				if (cnt*2 + i > dims) {
					break;
				}

				i = exec_repeat(output, i, loc[cur], cnt, width);
			}
		}
		prev = output[i-1];
	}
	// Write the last pixel in case width and height are both odd
	if (i < dims) {
		uint32_t dt;
		read_delta_codes(bs, &dt, 1, depth);
		output[i] = table_lookup(table, depth, prev, dt);
		++i;
	}
	return i;
}

static struct wu_st pi_decode_inner(struct wuimg *img, const uint8_t depth,
const struct wuptr data) {
	const size_t dims = wuimg_size(img);
	size_t written = 0;
	const unsigned colors = (1 << depth);
	const unsigned table_size = colors*colors;
	uint8_t *delta_table = malloc(table_size);
	if (delta_table) {
		init_delta_table(delta_table, colors);

		struct bitstrm bs;
		bitstrm_from_wuptr(&bs, data);
		written = bt_decode_loop(img->data, dims, &bs, img->w,
			delta_table, colors);
		free(delta_table);
	}
	return wuerr_partial(written, dims);
}

struct wu_st pi_decode(const struct pi_desc *desc, struct wuimg *img) {
	return pi_decode_inner(img, desc->depth, desc->data);
}

static bool pi_is_lsp(const uint8_t ext[static 4]) {
	const char *e = (const char *)ext;
	return PI_ACCEPT_ANY_EXT | !strcmp(e, "lsp") | !strcmp(e, "g");
}

struct wu_st pi_read_header(struct pi_desc *desc, struct wuimg *img,
const struct wuptr mem, const uint8_t ext[static 4]) {
	/* Pi header:
		Offset  Size    Name
		0       BYTE[2] Magic;          // "Pi"
		2       VAR     Comment[];      // 0x1a terminated
		--      VAR     Dummy[];        // 0x00 terminated

		+0      BYTE    ModeByte;       // Unreliable palette indicator
		+1      BYTE    ScreenRatioNum;
		+2      BYTE    ScreenRatioDen;
		+3      BYTE    BitDepth;       // 4 or 8
		+4      BYTE[4] SaverModelSig;  // Compressor model
		+8      WORD    SaverDataSize;
		+10     VAR     SaverData;      // Non-essential private data

		--      WORD    ImageWidth;
		+2      WORD    ImageHeight;
		+4      VAR     Palette;        // Length of 1 << BitDepth
	*/

	*desc = (struct pi_desc){0};
	struct mparser mp = mp_wuptr(mem);
	const unsigned char sig[] = {'P', 'i'};
	const uint8_t *buf = mp_slice(&mp, sizeof(sig));
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	if (!memcmp(buf, sig, sizeof(sig))) {
		if (!mp_upto(&mp, &desc->comm, 0x1a)
		|| !mp_upto(&mp, &desc->dummy, 0x00)) {
			return WUERR_HERE(wu_unexpected_eof);
		}

		buf = mp_slice(&mp, 10);
		if (!buf) {
			return WUERR_HERE(wu_unexpected_eof);
		}

		desc->depth = buf[3];
		switch (desc->depth) {
		case 4: case 8: break;
		default: return wuerr(wu_invalid_header,
			"depth neither 4 nor 8");
		}

		const uint8_t ratio_x = buf[1];
		const uint8_t ratio_y = buf[2];
		/* According to Google Translate, "dots are multiplied by n/m
		 * in the vertical direction", so swap parameter order. */
		wuimg_aspect_ratio(img, ratio_y, ratio_x);

		memcpy(desc->saver.model, buf + 4, sizeof(desc->saver.model));
		desc->saver.data.len = buf_endian16b(buf + 8);
		desc->saver.data.ptr = mp_slice(&mp, desc->saver.data.len);
		if (!desc->saver.data.ptr && desc->saver.data.len) {
			return WUERR_HERE(wu_unexpected_eof);
		}
	} else if (pi_is_lsp(ext)) {
		mp_seek_set(&mp, 0);
		desc->depth = 4;
		desc->lsp = true;
	} else {
		return WUERR_HERE(wu_invalid_signature);
	}

	const size_t elems = 1u << desc->depth;
	buf = mp_slice(&mp, 4 + 3*elems);
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	/* Images of width 2 or less are stored 'without repetition'. Maybe
	 * the bitstream omits the 'process delta again' bit then, but I
	 * don't have any samples to check that. Hence, this. */
	img->w = buf_endian16b(buf);
	img->h = buf_endian16b(buf + 2);
	img->channels = 1;
	img->bitdepth = 8;
	if (img->w > 2)  {
		struct palette *pal = wuimg_palette_init(img);
		if (pal) {
			const uint8_t *pal_src = buf + 4;
			palette_from_rgb8(pal, pal_src, elems);
			desc->data = mp_remaining(&mp);
			return WU_OK;
		}
		return WUERR_HERE(wu_alloc_error);
	} else if (desc->lsp) {
		return wuerr(wu_uncertain_validity, "width <= 2 in file with"
			" no signature. not a graphic file?");
	}
	return wuerr(wu_samples_wanted, "width <= 2");
}


// Excellents Yuugiri DPC
struct wu_st dpc_decode(const struct dpc_desc *desc, struct wuimg *img) {
	if (desc->data.len) {
		return pi_decode_inner(img, 4, desc->data);
	}
	for (int i = 0; i < 1 << 4; ++i) {
		img->data[i] = (uint8_t)i;
	}
	return WU_OK;
}

struct wu_st dpc_read_header(struct dpc_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* DPC header:
		Offset  Type    Name
		0       u16     Palette[16]
		32      u16     X
		34      u16     Y
		36      u16     Width
		38      u16     Height
		40
	*/
	struct mparser mp = mp_wuptr(mem);
	const struct wuptr buf = mp_avail(&mp, 40);
	if (buf.len < 32) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 4;
	struct palette *pal = wuimg_palette_init(img);
	if (!pal) {
		return WUERR_HERE(wu_alloc_error);
	}
	for (int i = 0; i < 16; ++i) {
		const uint16_t e = buf_endian16l(buf.ptr + i*2);
		if (e & 0x0842) {
			return wuerr(wu_invalid_header,
				"unused bits set in palette");
		}
		pal->color[i] = (struct pix_rgba8) {
			.g = (e >> 12) & 0xf,
			.r = (e >> 7) & 0xf,
			.b = (e >> 2) & 0xf,
			.a = (e & 1) ? 0x0 : 0xf,
		};
	}

	if (buf.len == 40) {
		desc->x = buf_endian16l(buf.ptr + 32);
		desc->y = buf_endian16l(buf.ptr + 34);
		img->w = buf_endian16l(buf.ptr + 36);
		img->h = buf_endian16l(buf.ptr + 38);
	} else {
		// Palette only file
		img->w = 4;
		img->h = 4;
	}
	desc->data = mp_remaining(&mp);
	return WU_OK;
}
