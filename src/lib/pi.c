// SPDX-License-Identifier: 0BSD
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "misc/bit.h"
#include "misc/file.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "pi.h"

/* Documented in
https://mooncore.eu/bunny/txt/pi-pic.htm
*/

// Enable to use slightly slower but clearly correct code.
//#define EXACT_BITS

enum pi_repeat_src {
	pi_last4 = 0,
	pi_1row = 1,
	pi_2row = 2,
	pi_1row_next = 6,
	pi_1row_prev = 7,
};

void pi_cleanup(struct pi_desc *desc) {
	wustr_free(&desc->comm);
	free(desc->saver.data);
}

static uint8_t table_lookup(uint8_t *table, const unsigned depth,
const size_t x, const size_t y) {
	return memcycle(table + x*depth, y);
}

static void init_delta_table(uint8_t *table, const size_t colors) {
	for (size_t x = 0; x < colors; ++x) {
		size_t xx = colors + x;
		for (size_t y = 0; y < colors; ++y) {
			table[y] = (uint8_t)(xx & (colors - 1));
			--xx;
		}
		table += colors;
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

	if (i < diff && cnt) {
		const uint8_t pair[2] = {
			output[diff & 1],
			output[(diff & 1) ^ 1],
		};
		do {
			memcpy(output + i, pair, sizeof(pair));
			i += 2;
			--cnt;
		} while (i < diff && cnt);
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

static size_t read_8bit_delta(struct bitstrm *bs) {
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
#ifdef EXACT_BITS
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
#else
	const uint32_t word = bitstrm_msb_peek_high25(bs);
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
	bitstrm_seek(bs, read);
	return (word >> (32 - read)) ^ xor;
#endif
}

static size_t read_4bit_delta(struct bitstrm *bs) {
	/* 4-bit delta encoding:
		Code    Values
		1x      0-1
		00x     2-3
		010xx   4-7
		011xxx  8-15
	*/
#ifdef EXACT_BITS
	if (bitstrm_msb_next(bs)) {
		return bitstrm_msb_next(bs);
	} else {
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
#else
	const uint32_t word = bitstrm_msb_peek_high25(bs);
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
	bitstrm_seek(bs, read);
	return (word >> (32 - read)) ^ xor;
#endif
}

static size_t bt_decode_loop(uint8_t *restrict output, const size_t dims,
struct bitstrm *bs, const size_t width, uint8_t *restrict table,
const unsigned depth) {
	size_t i = 0;
	for (uint8_t prev = 0; i < dims - 1 && !bs->eof; prev = output[i-1]) {
		size_t dt[2];
		if (depth == 1 << 4) {
			dt[0] = read_4bit_delta(bs);
			dt[1] = read_4bit_delta(bs);
		} else {
			dt[0] = read_8bit_delta(bs);
			dt[1] = read_8bit_delta(bs);
		}
		output[i] = table_lookup(table, depth, prev, dt[0]);
		output[i+1] = table_lookup(table, depth, output[i], dt[1]);
		i += 2;

		if (i >= dims) {
			break;
		} else if (i == 2 || !bitstrm_msb_next(bs)) {
			enum pi_repeat_src loc[2];
			loc[1] = depth; // sentinel value
			for (int cur = 0;; cur = !cur) {
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
	}
	return i;
}

static size_t max_bitstream_size(FILE *ifp, const size_t dims) {
	return zumin(dims * 2, file_remaining(ifp));
}

size_t pi_decode(const struct pi_desc *desc, struct wuimg *img) {
	size_t written = 0;
	if (wuimg_alloc_noverify(img)) {
		const size_t dims = wuimg_size(img);
		const unsigned colors = (1 << desc->depth);
		const unsigned table_size = colors*colors;
		const size_t bslen = max_bitstream_size(desc->ifp, dims);
		void *buf = malloc(table_size + bslen);
		if (buf) {
			uint8_t *restrict delta_table = buf;
			uint8_t *restrict bitstream = delta_table + table_size;

			init_delta_table(delta_table, colors);
			const size_t read = fread(bitstream, 1, bslen, desc->ifp);

			struct bitstrm bs = bitstrm_from_bytes(bitstream, read);
			written = bt_decode_loop(img->data, dims, &bs, img->w,
				delta_table, colors);
			free(buf);
		}
	}
	return written;
}

static enum wu_error validate_header(struct pi_desc *desc, struct wuimg *img,
uint8_t pixel_x, uint8_t pixel_y, const uint8_t bitdepth, const uint16_t width,
const uint16_t height) {
	switch (bitdepth) {
	case 4: case 8:
		break;
	default:
		return wu_invalid_header;
	}

	/* Images of width 2 or less are stored 'without repetition'. I guess
	 * the bitstream may omit the 'process delta again' bit then, but I
	 * don't have any samples to check that. Hence, this. */
	if (width <= 2)  {
		return wu_unsupported_feature;
	}

	img->w = width;
	img->h = height;
	img->channels = 1;
	img->bitdepth = 8;
	wuimg_aspect_ratio(img, pixel_y, pixel_x);

	desc->depth = bitdepth;
	return wu_ok;
}

enum wu_error pi_read_header(struct pi_desc *desc, struct wuimg *img) {
	/* Pi header (after magic bytes):
		Offset  Size    Name
		0       VAR     Comment[];      // 0x1a then 0x00 terminated

		--      BYTE    ModeByte;       // Unreliable palette indicator
		+1      BYTE    PixelX;         // Aspect ratio numerator
		+2      BYTE    PixelY;
		+3      BYTE    BitDepth;       // 4 or 8
		+4      BYTE[4] SaverModelSig;  // Compressor model
		+8      WORD    SaverDataSize;
		+10     VAR     SaverData;      // Non-essential private data

		--      WORD    ImageWidth;
		+2      WORD    ImageHeight;
		+4      VAR     Palette;        // Length of 1 << BitDepth
	*/

	if (!file_read_pi_comm(&desc->comm, desc->ifp)) {
		return wu_unexpected_eof;
	}

	uint8_t buf[10];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	memcpy(desc->saver.sig, buf + 4, sizeof(desc->saver.sig));
	unsigned short saver_len = buf_endian16(buf + 8, big_endian);
	if (saver_len) {
		desc->saver.len = saver_len;
		desc->saver.data = malloc(saver_len);
		if (!desc->saver.data) {
			return wu_alloc_error;
		}
		if (!fread(desc->saver.data, saver_len, 1, desc->ifp)) {
			return wu_unexpected_eof;
		}
	}

	if (!fread(buf + 4, 4, 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	enum wu_error status = validate_header(desc, img,
		buf[1], buf[2], buf[3],
		buf_endian16(buf + 4, big_endian),
		buf_endian16(buf + 6, big_endian));
	if (status != wu_ok) {
		return status;
	}

	struct raster_pal *pal = wuimg_palette_init(img);
	if (!pal) {
		return wu_alloc_error;
	}
	status = fmt_load_pal(desc->ifp, pal, fmt_pal_rgb, 1 << desc->depth);
	if (status == wu_ok) {
		return wuimg_verify(img);
	}
	return status;
}

enum wu_error pi_open_file(struct pi_desc *desc, FILE *ifp) {
	*desc = (struct pi_desc) {
		.ifp = ifp,
	};
	const unsigned char sig[] = {'P', 'i'};
	return fmt_sigcmp(sig, sizeof(sig), ifp);
}
