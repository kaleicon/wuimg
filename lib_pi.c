#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

#include "common.h"
#include "common_composite.h"
#include "lib_pi.h"

// Define to use an algorithm that's a bit slower but clearly correct for decoding.
//#define EXACT_BITS

void pi_cleanup(struct pi_desc *desc) {
	free(desc->palette);
	free(desc->comment);
	free(desc->saver);
}

static unsigned char table_lookup(unsigned char *table,
const unsigned int colors, const unsigned int x, const unsigned int y) {
	table += colors * x;
	unsigned char val = table[y];
	memmove(table + 1, table, y);
	table[0] = val;
	return val;
}

static void init_delta_table(unsigned char *table, const unsigned int colors) {
	for (unsigned int x = 0; x < colors; ++x) {
		for (unsigned int y = 0; y < colors; ++y) {
			table[y] = (unsigned char)((colors + x - y) % colors);
		}
		table += colors;
	}
}

static u_int32_t read_bits(const unsigned char *restrict bitstream,
size_t *restrict bitpos, unsigned int n) {
	u_int32_t bits = 0;
	while (n) {
		unsigned byte = bitstream[*bitpos / 8];
		do {
			bits <<= 1;
			bits |= ((byte << (*bitpos % 8)) & 0x80) ? 1 : 0;
			++(*bitpos);
			--n;
		} while (n && *bitpos % 8);
	}
	return bits;
}

static size_t exec_repeat(unsigned char *restrict output, size_t i,
const enum pi_repeat_src loc, size_t cnt, const size_t width) {
	size_t diff;
	switch (loc) {
	case pi_last4:
		if (output[i-2] == output[i-1]) {
			memset(output + i, output[i-1], cnt*2);
			return i + cnt*2;
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
		diff = width;
		break;
	case pi_2row:
		diff = width * 2;
		break;
	case pi_1row_next:
		diff = width - 1; // Invert sign because this is the difference
		break;
	case pi_1row_prev:
		diff = width + 1;
		break;
	default: // Prevent 'unitialized variable' errors
		return i;
	}

	const size_t oddness = diff & 1;
	while (i < diff && cnt) {
		output[i] = output[oddness];
		output[i+1] = output[oddness ^ 1];
		i += 2;
		--cnt;
	}

	while (cnt > diff / 2) { // Using memcpy or memmove is a grave mistake.
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

#ifndef EXACT_BITS
static u_int32_t read_dword(const unsigned char *restrict bs,
const size_t bitpos, const bool full_bits) {
	size_t i = bitpos / 8;
	size_t o = bitpos % 8;
	const u_int32_t f = buf_endian32(bs + i, big_endian);
	if (full_bits) {
		return (f << o) | (u_int32_t)bs[i+4] >> (8 - o);
	}
	return f << o;
}

static u_int16_t read_word(const unsigned char *restrict bs,
const size_t bitpos, const bool full_bits) {
	size_t i = bitpos / 8;
	size_t o = bitpos % 8;
	const u_int16_t f = (u_int16_t)(buf_endian16(bs + i, big_endian) << o);
	if (full_bits) {
		return f | (u_int16_t)(bs[i+2] >> (8 - o));
	}
	return f;
}
#endif

static size_t read_repeat_cnt(const unsigned char *restrict bs,
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

	unsigned int seq_len = 0;

#ifdef EXACT_BITS
	while (read_bits(bs, bitpos, 1) && seq_len < 31) {
		++seq_len;
	}
	return read_bits(bs, bitpos, seq_len) | (1U << seq_len);
#else
	const u_int32_t mask = 1U << 31;
	u_int32_t dword = read_dword(bs, *bitpos, true);
	while ((dword << seq_len) & mask && seq_len < 31) {
		++seq_len;
	}

	*bitpos += seq_len;
	dword = read_dword(bs, *bitpos, true); // Read also the beginning zero
	*bitpos += seq_len + 1;
	return (dword | mask) >> (31 - seq_len);
#endif
}

static enum pi_repeat_src read_repeat_loc(const unsigned char *restrict bs,
size_t *restrict bitpos) {
	/* Location codes: 00, 01, 10, 110, 111 */
#ifdef EXACT_BITS
	const u_int32_t bits = read_bits(bs, bitpos, 2);
	switch (bits) {
	case 0: case 1: case 2:
		return bits;
	}
	return (bits << 1) | read_bits(bs, bitpos, 1);
#else
	const unsigned int word = read_word(bs, *bitpos, false) >> 13;
	unsigned int diff;
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

static unsigned int read_8bit_delta(const unsigned char *restrict bs,
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

	if (read_bits(bs, bitpos, 1)) {
		return read_bits(bs, bitpos, 1);
	} else { // 00
		unsigned int sh = 0;
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
}

static unsigned read_4bit_delta(const unsigned char *restrict bs,
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
	const unsigned int word = read_word(bs, *bitpos, false);
	unsigned int diff, mask;
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

static inline unsigned char * bt_decode_loop(unsigned char *restrict output,
const size_t dims, const unsigned char *restrict bitstream, const size_t bitlen,
const size_t width, const int bitdepth) {
	const unsigned int colors = 1U << bitdepth;
	unsigned char *table = malloc(colors*colors);
	if (!table) {
		free(output);
		return NULL;
	}
	init_delta_table(table, colors);

	size_t i = 0;
	size_t bitpos = 0;
	unsigned char prev = 0;

	while (i < dims && bitpos < bitlen) {
		unsigned int dt1, dt2;
		if (bitdepth == 4) {
			dt1 = read_4bit_delta(bitstream, &bitpos);
			dt2 = read_4bit_delta(bitstream, &bitpos);
		} else {
			dt1 = read_8bit_delta(bitstream, &bitpos);
			dt2 = read_8bit_delta(bitstream, &bitpos);
		}
		output[i] = table_lookup(table, colors, prev, dt1);
		output[i+1] = table_lookup(table, colors, output[i], dt2);
		i += 2;
		if (i >= dims) {
			break;
		} else if (i != 2 && read_bits(bitstream, &bitpos, 1)) {
			prev = output[i-1];
			continue;
		}

		enum pi_repeat_src prev_loc;
		enum pi_repeat_src loc = read_repeat_loc(bitstream, &bitpos);
		do {
			size_t cnt = read_repeat_cnt(bitstream, &bitpos)
				- (i == 2 ? 1 : 0);
			cnt = zumin((dims - i) / 2, cnt);

			i = exec_repeat(output, i, loc, cnt, width);

			prev_loc = loc;
			loc = read_repeat_loc(bitstream, &bitpos);
		} while (loc != prev_loc && i < dims);
		prev = output[i-1];
	}

	if (i < dims) {
		puts(RASTER_EOF);
	}
	free(table);
	return output;
}

static unsigned char * bt4_decode(unsigned char *restrict output,
const size_t dims, const unsigned char *restrict bitstream,
const size_t bitlen, const size_t width) {
	return bt_decode_loop(output, dims, bitstream, bitlen, width, 4);
}

static unsigned char * bt8_decode(unsigned char *restrict output,
const size_t dims, const unsigned char *restrict bitstream,
const size_t bitlen, const size_t width) {
	return bt_decode_loop(output, dims, bitstream, bitlen, width, 8);
}

static size_t max_bitstream_size(FILE *ifp, const size_t dims) {
	const long start = ftell(ifp);
	fseek(ifp, 0, SEEK_END);
	const long end = ftell(ifp);
	fseek(ifp, start, SEEK_SET);
	return zumin(dims * 2, (size_t)(end - start));
}

unsigned char * pi_decode(const struct pi_desc *desc) {
	const size_t dims = desc->w * desc->h;
	unsigned char *output = malloc(dims);
	if (!output) {
		return NULL;
	}

	/* The spec recommends that the last 32 bits be zero, so we'll
	 * enforce this to do away with most bounds checks. */
	const size_t bslen = max_bitstream_size(desc->ifp, dims);
	unsigned char *bitstream = malloc(bslen + 4);
	if (!bitstream) {
		free(output);
		return NULL;
	}
	const size_t read = fread(bitstream, 1, bslen, desc->ifp);
	if (read != bslen) {
		puts(RASTER_EOF);
	}
	memset(bitstream + read, 0, 4);

	const size_t bitlen = read * 8;
	switch (desc->bitdepth) {
	case 4:
		bt4_decode(output, dims, bitstream, bitlen, desc->w);
		break;
	case 8:
		bt8_decode(output, dims, bitstream, bitlen, desc->w);
		break;
	default:
		free(output);
		output = NULL;
	}
	free(bitstream);
	return output;
}

unsigned char * pi_take_palette(struct pi_desc *desc) {
	unsigned char *pal = (unsigned char *)desc->palette;
	desc->palette = NULL;
	return pal;
}

static enum lib_fail load_palette(struct pi_desc *desc) {
	struct colormap *pal = malloc(256 * 4);
	if (!pal) {
		return lib_alloc_error;
	}

	const size_t pal_len = (1U << desc->bitdepth);
	unsigned char *restrict buf = ((unsigned char *)pal) + pal_len;
	if (fread(buf, 1, pal_len * 3, desc->ifp) != pal_len * 3) {
		free(pal);
		return lib_unexpected_eof;
	}

	for (size_t i = 0; i < pal_len; ++i) {
		pal[i].r = buf[i*3];
		pal[i].g = buf[i*3 + 1];
		pal[i].b = buf[i*3 + 2];
		pal[i].a = 0xff;
	}

	desc->palette = pal;
	return lib_ok;
}

static enum lib_fail validate_header(struct pi_desc *desc, u_int8_t pixel_x,
u_int8_t pixel_y, const u_int8_t bitdepth, const u_int16_t width,
const u_int16_t height) {
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

	desc->w = width;
	desc->h = height;
	desc->bitdepth = bitdepth;
	desc->pixel_x = pixel_x;
	desc->pixel_y = pixel_y;
	return lib_ok;
}

static enum lib_fail read_comment(struct pi_desc *desc) {
	const unsigned char eoc = 0x1a;
	int c = getc(desc->ifp);
	const int sec_c = getc(desc->ifp);
	if (c == EOF || sec_c == EOF) {
		return lib_unexpected_eof;
	} else if ((c == 0 || c == eoc) && sec_c == 0) {
		return lib_ok;
	}

	size_t size = 80;
	desc->comment = malloc(size);
	if (!desc->comment) {
		return lib_alloc_error;
	}
	desc->comment[0] = (unsigned char)c;
	desc->comment[1] = (unsigned char)sec_c;

	// Text ends with a 0x1A, but the comment area ends with a null byte.
	size_t len = 2;
	bool found_eoc = (c == eoc || sec_c == eoc);
	while ((c = getc(desc->ifp)) != EOF) {
		if (len == USHRT_MAX) {
			if (found_eoc) {
				break;
			}
			return lib_pi_comment_too_long;
		} else if (len == size) {
			size += size / 4;
			unsigned char *hold = realloc(desc->comment, size);
			if (!hold) {
				return lib_alloc_error;
			}
			desc->comment = hold;
		}

		if (c == eoc && !found_eoc) {
			found_eoc = true;
			desc->comment_len = (unsigned short)len;
		}

		desc->comment[len] = (unsigned char)c;
		if (!c) {
			break;
		}
		++len;
	}
	if (c == EOF) {
		return lib_unexpected_eof;
	}
	desc->comment_area_len = (unsigned short)len;
	return lib_ok;
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
		3       BYTE    BitDepth;
		4       BYTE[4] SaverModelSig;  // Compressor model
		8       WORD    SaverDataSize;
		10      VAR     SaverData;      // Non-essential private data

		--      WORD    ImageWidth;
		+2      WORD    ImageHeight;
		+4
	*/

	u_int8_t buf[10];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return lib_unexpected_eof;
	}

	memcpy(desc->saver_sig, buf + 4, sizeof(desc->saver_sig));
	unsigned short saver_len = buf_endian16(buf + 8, big_endian);
	if (saver_len) {
		desc->saver_len = saver_len;
		desc->saver = malloc(saver_len);
		if (!desc->saver) {
			return lib_alloc_error;
		}
		if (fread(desc->saver, 1, saver_len, desc->ifp) != saver_len) {
			return lib_unexpected_eof;
		}
	}

	if (fread(buf + 4, 1, 4, desc->ifp) != 4) {
		return lib_unexpected_eof;
	}

	status = validate_header(desc, buf[1], buf[2], buf[3],
		buf_endian16(buf + 4, big_endian),
		buf_endian16(buf + 6, big_endian));
	if (status != lib_ok) {
		return status;
	}

	return load_palette(desc);
}

enum lib_fail pi_open_file(FILE *ifp, struct pi_desc *desc) {
	memset(desc, 0, sizeof(*desc));

	char buf[2];
	if (fread(buf, 1, sizeof(buf), ifp) == sizeof(buf)) {
		if (!memcmp(buf, "Pi", sizeof(buf))) {
			desc->ifp = ifp;
			return lib_ok;
		}
		return lib_invalid_signature;
	}
	return lib_unexpected_eof;
}
