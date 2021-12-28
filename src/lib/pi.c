#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "../common.h"
#include "../wustr.h"
#include "pi.h"

// Define to use slightly slower but clearly correct code.
//#define EXACT_BITS

enum pi_repeat_src {
	pi_last4 = 0,
	pi_1row = 1,
	pi_2row = 2,
	pi_1row_next = 6,
	pi_1row_prev = 7,
};

void pi_cleanup(struct pi_desc *desc) {
	raster_free(&desc->rast);
	free(desc->comment.data);
	free(desc->saver.data);
}

static uint8_t table_lookup(uint8_t *table, const size_t y) {
	const uint8_t val = table[y];
	memmove(table + 1, table, y);
	*table = val;
	return val;
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
		if (output[i-2] == output[i-1]) {
			memset(output + i, output[i-1], cnt*2);
			i += cnt*2;
		} else {
			diff = (i == 2) ? 2 : 4;
			while (cnt) {
				output[i] = output[i - diff];
				output[i+1] = output[i+1 - diff];
				i += 2;
				--cnt;
			}
		}
		return i;
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

	const size_t oddness = diff & 1;
	while (i < diff && cnt) {
		output[i] = output[oddness];
		output[i+1] = output[oddness ^ 1];
		i += 2;
		--cnt;
	}

	while (cnt > diff/2) {
		output[i] = output[i - diff];
		output[i+1] = output[i+1 - diff];
		i += 2;
		--cnt;
	}

	if (cnt) {
		memcpy(output + i, output + i - diff, cnt*2);
		i += cnt*2;
	}
	return i;
}

static uint32_t read_bits(const uint8_t *restrict bitstream,
size_t *restrict bitpos, unsigned long n) {
	uint32_t bits = 0;
	while (n) {
		const unsigned byte = bitstream[*bitpos / 8];
		do {
			bits <<= 1;
			bits |= ((byte << (*bitpos % 8)) & 0x80) ? 1 : 0;
			++(*bitpos);
			--n;
		} while (n && *bitpos % 8);
	}
	return bits;
}

#ifndef EXACT_BITS
static uint32_t current_dword(const uint8_t *restrict bs,
const size_t bitpos, const bool full_bits) {
	size_t i = bitpos / 8;
	size_t o = bitpos % 8;
	const uint32_t f = buf_endian32(bs + i, big_endian) << o;
	if (full_bits) {
		return f | (unsigned)(bs[i+4] >> (8 - o));
	}
	return f;
}

static uint16_t current_word(const uint8_t *restrict bs,
const size_t bitpos, const bool full_bits) {
	size_t i = bitpos / 8;
	size_t o = bitpos % 8;
	const uint16_t f = (uint16_t)(buf_endian16(bs + i, big_endian) << o);
	if (full_bits) {
		return f | (uint16_t)(bs[i+2] >> (8 - o));
	}
	return f;
}
#endif

static uint32_t read_repeat_cnt(const uint8_t *restrict bs,
size_t *restrict bitpos) {
	/* Repeat count encoding:
		Coding  Range
		0       1
		10x     2-3
		110xx   4-7
		1110xxx 8-15
	 * and so on and so on. The length is unbounded, but since the image
	 * dimensions are defined in 16 bits no valid code can span more than
	 * 32 bits. Add to this that one bit is implied and that the count is
	 * for pairs of pixels, thus the max length to check is 30 bits. */

	uint_fast32_t seq_len = 0;

#ifdef EXACT_BITS
	while (read_bits(bs, bitpos, 1) && seq_len < 31) {
		++seq_len;
	}
	return read_bits(bs, bitpos, seq_len) | (1U << seq_len);
#else
	const uint32_t mask = 1U << 31;
	const uint32_t repeat = current_dword(bs, *bitpos, true);
	while ((repeat << seq_len) & mask && seq_len < 31) {
		++seq_len;
	}

	*bitpos += seq_len;
	// The beginning zero is included
	const uint32_t payload = current_dword(bs, *bitpos, true);
	*bitpos += seq_len + 1;
	return (payload | mask) >> (31 - seq_len);
#endif
}

static enum pi_repeat_src read_repeat_loc(const uint8_t *restrict bs,
size_t *restrict bitpos) {
	/* Location codes: 00, 01, 10, 110, 111 */
#ifdef EXACT_BITS
	const uint32_t bits = read_bits(bs, bitpos, 2);
	switch (bits) {
	case 0: case 1: case 2:
		return bits;
	}
	return (bits << 1) | read_bits(bs, bitpos, 1);
#else
	const uint_fast32_t word = current_word(bs, *bitpos, false) >> 13;
	uint8_t diff;
	switch (word) {
	case 0: case 1: case 2: case 3: case 4: case 5:
		diff = 2; break;
	default:
		diff = 3;
	}
	*bitpos += diff;
	return word >> (3 - diff);
#endif
}

static size_t read_8bit_delta(const uint8_t *restrict bs,
size_t *restrict bitpos) {
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
	if (read_bits(bs, bitpos, 1)) {
		return read_bits(bs, bitpos, 1);
	} else { // 00
		uint32_t sh = 0;
		// 010
		if (read_bits(bs, bitpos, 1)) { // Weee
			// 0110
			if (read_bits(bs, bitpos, 1)) { // eeee
				// 01110
				if (read_bits(bs, bitpos, 1)) { // eeee
					// 011110
					if (read_bits(bs, bitpos, 1)) { // eeee
						// 0111110
						if (read_bits(bs, bitpos, 1)) { // eeee
							// 0111111
							if (read_bits(bs, bitpos, 1)) {
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
		return read_bits(bs, bitpos, sh) | (1U << sh);
	}
#else
	const uint32_t word = current_dword(bs, *bitpos, false);
	uint32_t read, mask;
	if (word >= 0x01U << (32 - 1)) { // 1x
		read = 2; mask = 0x01 << 1;
	} else if (word >= 0x3fU << (32 - 7)) { // 0111111xxxxxxx
		read = 14; mask = 0x1f << 8;
	} else if (word >= 0x1fU << (32 - 6)) { // 0111110xxxxxx
		read = 13; mask = 0x3f << 6;
	} else if (word >= 0x0fU << (32 - 5)) { //  011110xxxxx
		read = 11; mask = 0x1f << 5;
	} else if (word >= 0x07U << (32 - 4)) { //   01110xxxx
		read = 9; mask = 0x0f << 4;
	} else if (word >= 0x03U << (32 - 3)) { //    0110xxx
		read = 7; mask = 0x07 << 3;
	} else if (word >= 0x01U << (32 - 2)) { //     010xx--
		read = 5; mask = 0x03 << 2;
	} else {                                //      00x----
		read = 3; mask = 0x01 << 1;
	}
	*bitpos += read;
	return (word >> (32 - read)) ^ mask;
#endif
}

static size_t read_4bit_delta(const uint8_t *restrict bs,
size_t *restrict bitpos) {
	/* 4-bit delta encoding:
		Code    Values
		1x      0-1
		00x     2-3
		010xx   4-7
		011xxx  8-15
	*/
#ifdef EXACT_BITS
	if (read_bits(bs, bitpos, 1)) {
		return read_bits(bs, bitpos, 1);
	} else {
		unsigned int sh = 0;
		if (read_bits(bs, bitpos, 1)) {
			if (read_bits(bs, bitpos, 1)) {
				++sh;
			}
			++sh;
		}
		++sh;
		return read_bits(bs, bitpos, sh) | (1U << sh);
	}
#else
	const uint32_t word = current_word(bs, *bitpos, false);
	uint32_t diff, mask;
	switch (word >> 13) {
	case 0: case 1:
		diff = 3; mask = 0x02;
		break;
	case 2:
		diff = 5; mask = 0x0c;
		break;
	case 3:
		diff = 6; mask = 0x10;
		break;
	default:
		diff = 2; mask = 0x02;
		break;
	}
	*bitpos += diff;
	return (word >> (16 - diff)) ^ mask;
#endif
}
static size_t bt_decode_loop(uint8_t *restrict output,
const size_t dims, const uint8_t *restrict bitstream, const size_t bitlen,
const size_t width, uint8_t *restrict table, const size_t colors) {
	size_t i = 0;
	size_t bitpos = 0;
	for (uint8_t prev = 0; i < dims && bitpos < bitlen; prev = output[i-1]) {
		size_t dt[2];
		if (colors == 1 << 4) {
			dt[0] = read_4bit_delta(bitstream, &bitpos);
			dt[1] = read_4bit_delta(bitstream, &bitpos);
		} else {
			dt[0] = read_8bit_delta(bitstream, &bitpos);
			dt[1] = read_8bit_delta(bitstream, &bitpos);
		}
		output[i] = table_lookup(table + colors * prev, dt[0]);
		output[i+1] = table_lookup(table + colors * output[i], dt[1]);
		i += 2;

		if (i >= dims) {
			break;
		} else if (i == 2 || !read_bits(bitstream, &bitpos, 1)) {
			enum pi_repeat_src loc[2];
			loc[1] = colors; // invalid value
			for (int cur = 0;; cur ^= 1) {
				loc[cur] = read_repeat_loc(bitstream, &bitpos);
				if (loc[cur] == loc[cur ^ 1]) {
					break;
				}

				size_t cnt = read_repeat_cnt(bitstream, &bitpos);
				if (i == 2) {
					--cnt;
				}
				if (cnt*2 + i >= dims) {
					break;
				}

				i = exec_repeat(output, i, loc[cur], cnt, width);
			}
		}
	}
	return i;
}

static size_t max_bitstream_size(FILE *ifp, const size_t dims) {
	return zumin(dims * 2, (size_t)file_get_remaining(ifp));
}

uint8_t * pi_decode(const struct pi_desc *desc) {
	const size_t dims = raster_size(&desc->rast);
	uint8_t *output = malloc(dims);
	if (!output) {
		return NULL;
	}

	const unsigned colors = 1 << desc->depth;
	const size_t bslen = max_bitstream_size(desc->ifp, dims);
	/* The spec recommends that the last 32 bits be zero, and we'll
	 * enforce this to do away with most bounds checks in the middle
	 * of decoding. */
	uint8_t *buf = malloc(colors*colors + bslen + 4);
	if (!buf) {
		free(output);
		return NULL;
	}

	init_delta_table(buf, colors);

	uint8_t *restrict bitstream = buf + colors*colors;
	const size_t read = fread(bitstream, 1, bslen, desc->ifp);
	memset(bitstream + read, 0, 4);

	const size_t bitlen = read * 8;
	const size_t written = bt_decode_loop(output, dims, bitstream, bitlen,
		desc->rast.w, buf, colors);
	free(buf);

	if (written < dims) {
		puts(RASTER_EOF);
	}
	return output;
}

static enum lib_fail validate_header(struct pi_desc *desc, uint8_t pixel_x,
uint8_t pixel_y, const uint8_t bitdepth, const uint16_t width,
const uint16_t height) {
	switch (bitdepth) {
	case 4: case 8:
		break;
	default:
		return lib_invalid_header;
	}

	if (!width || !height) {
		return lib_invalid_header;
	}

	if (!pixel_x || !pixel_y) {
		pixel_x = 1;
		pixel_y = 1;
	}

	desc->rast = (struct raster_desc) {
		.w = width,
		.h = height,
		.ch = 1,
		.bitdepth = 8,
	};
	desc->depth = bitdepth;
	desc->pixel_x = pixel_x;
	desc->pixel_y = pixel_y;
	return lib_ok;
}

static enum lib_fail read_comment(struct pi_desc *desc) {
	unsigned char buf[2];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return lib_unexpected_eof;
	}
	if (!buf[0] || (buf[0] == 0x1a && !buf[1])) {
		return lib_ok;
	}

	struct pi_comment *comm = &desc->comment;
	struct wugrow grow = wugrow_init(1);
	grow.pos = sizeof(buf);
	if (!wugrow_recheck(&comm->data, &grow)) {
		return lib_alloc_error;
	}
	memcpy(comm->data, buf, sizeof(buf));
	comm->text_len = (buf[1] == 0x1a);

	while (!comm->area_len && grow.pos < USHRT_MAX) {
		const int c = getc(desc->ifp);
		if (!wugrow_recheck(&comm->data, &grow)) {
			return lib_alloc_error;
		}

		comm->data[grow.pos] = (unsigned char)c;
		switch (c) {
		case EOF:
			return lib_unexpected_eof;
		case 0:
			comm->area_len = (unsigned short)grow.pos;
			// fallthrough
		case 0x1a:
			if (!comm->text_len) {
				comm->text_len = (unsigned short)grow.pos;
			}
			break;
		}
		++grow.pos;
	}
	return comm->area_len ? lib_ok : lib_pi_comment_too_long;
}

enum lib_fail pi_read_header(struct pi_desc *desc) {
	enum lib_fail status = read_comment(desc);
	if (status != lib_ok) {
		return status;
	}

	/* Pi header (after magic bytes and comment):
		Offset  Size    Name
		0       BYTE    ModeByte;       // Unreliable palette indicator
		1       BYTE    PixelX;         // Aspect ratio numerator
		2       BYTE    PixelY;
		3       BYTE    BitDepth;       // 4 or 8
		4       BYTE[4] SaverModelSig;  // Compressor model
		8       WORD    SaverDataSize;
		10      VAR     SaverData;      // Non-essential private data

		--      WORD    ImageWidth;
		+2      WORD    ImageHeight;
		+4      VAR     Palette;        // Length of 1 << BitDepth
	*/

	uint8_t buf[10];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	memcpy(desc->saver.sig, buf + 4, sizeof(desc->saver.sig));
	unsigned short saver_len = buf_endian16(buf + 8, big_endian);
	if (saver_len) {
		desc->saver.len = saver_len;
		desc->saver.data = malloc(saver_len);
		if (!desc->saver.data) {
			return lib_alloc_error;
		}
		if (!fread(desc->saver.data, saver_len, 1, desc->ifp)) {
			return lib_unexpected_eof;
		}
	}

	if (!fread(buf + 4, 4, 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	status = validate_header(desc, buf[1], buf[2], buf[3],
		buf_endian16(buf + 4, big_endian),
		buf_endian16(buf + 6, big_endian));
	if (status != lib_ok) {
		return status;
	}

	status = lib_load_pal(desc->ifp, &desc->rast.palette, lib_pal_rgb,
		1 << desc->depth);
	if (status != lib_ok) {
		return status;
	}

	raster_normalize(&desc->rast);
	return lib_ok;
}

enum lib_fail pi_open_file(struct pi_desc *desc, FILE *ifp) {
	const unsigned char sig[] = {'P', 'i'};
	const enum lib_fail st = lib_sigcmp(sig, sizeof(sig), ifp);
	if (st == lib_ok) {
		memset(desc, 0, sizeof(*desc));
		desc->ifp = ifp;
	}
	return st;
}
