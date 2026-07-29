// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "pic2.h"

/* Yanagisawa's PIC2 format. Studied from
https://www.vector.co.jp/soft/mac/art/se034104.html
 * and
https://github.com/jasper-software/xv/blob/main/src/xvpic2.c

 * There seems to be no written spec like with Pi and PIC, which is a pity.
 * Would also like to know what became of Yanagisawa.

 * Some format info (doesn't even have it's own page):
https://ja.wikipedia.org/wiki/PIC_(%E7%94%BB%E5%83%8F%E5%9C%A7%E7%B8%AE)#PIC2
 * which finishes with:
 * "As of 2025, no material about PIC2 can be found on the Internet."

 * * *

 * Due to a lack of samples, only arithmetic decoding with 24- and 15-bit
 * images is supported. xvpic2.c says any depth % 3 == 0 is possible, and in
 * theory, this decoder should be able to handle those too.

 * TODO: find samples for
 * - 16-bit, 8-bit, and paletted images
 * - RLE and raw images
 * - files with multiple images
*/

static const uint8_t PIC2_CHAIN_SH = 8;

const char * pic2_encoding_str(const enum pic2_id id) {
	switch (id) {
	case pic2_end: case pic2_pdpi:
		break;
	case pic2_p2bi:
		return "Raw, Intel-endian (P2BI)";
	case pic2_p2bm:
		return "Raw, Motorola-endian (P2BM)";
	case pic2_p2ss:
		return "Arithmetic (P2SS)";
	case pic2_p2sf:
		return "Run-Length (P2SF)";
	}
	return "???";
}

void pic2_rewind(struct pic2_desc *desc) {
	desc->mp.pos = desc->start;
}

enum arith_code {
	/* 8 < code < 15 unused */

	arith_cache_hit = 15,
	arith_cache_miss = 16,
	arith_cache_shuf = 17,

	arith_green = 32,
	arith_red = 48,
	arith_blue = 64,

	arith_chain_base = 80,
	arith_chain_center = 80 + 6,
	arith_chain_left = 80 + 6*2,
	arith_chain_right = 80 + 6*3,
	arith_chain_left2 = 80 + 6*4,
	arith_chain_right2 = 80 + 6*5,

	arith_last_code = 80 + 6*6,
};

struct arith_cache {
	uint32_t c[512][32];
};

struct arith_state {
	uint8_t *v;
	struct arith_cache *cache;
	uint32_t *row_cur;
	uint32_t *row_next;

	uint16_t *pos_cur;
	uint16_t *pos_next;
	uint16_t *pos_next2;

	struct bitstrm bs;
	uint16_t upper, lower;
	uint16_t prob[arith_last_code];
	uint8_t cache_last[512];
	enum arith_code cache_code;
};

static void arith_state_cycle(struct arith_state *st) {
	void *tmp = st->pos_cur;
	st->pos_cur = st->pos_next;
	st->pos_next = st->pos_next2;
	st->pos_next2 = tmp;

	tmp = st->row_cur;
	st->row_cur = st->row_next;
	st->row_next = tmp;
}

static bool arith_state_alloc(struct arith_state *st, size_t len) {
	const size_t color_size = len*2 * sizeof(*st->row_cur);

	// For flags, reserve space from -1 to len + 2
	size_t start_pad = 1;
	size_t flag_len = len + 2;
	const size_t flag_size = (flag_len*3 + start_pad) * sizeof(*st->pos_cur);

	st->v = calloc(1, sizeof(*st->cache) + flag_size + color_size);
	st->cache = (struct arith_cache *)st->v;

	st->row_cur = (uint32_t *)(st->cache + 1);
	st->row_next = st->row_cur + len;

	uint16_t *base = (uint16_t *)(st->row_cur + len*2) + start_pad;
	st->pos_cur = base;
	st->pos_next = base + flag_len;
	st->pos_next2 = base + flag_len*2;
	return st->v;
}

static bool arithmetic_decode_bit(struct arith_state *st,
const enum arith_code c) {
	/* 0xffff >= st->upper >= 0x8000
	 *           st->upper > st->lower >= 0 */
	const unsigned mul = (st->upper >> 8);
	/* ps needs to be >= 1, which is guaranteed by making prob >= 2
	 * when loading */
	const uint16_t ps = (uint16_t)((mul * st->prob[c]) >> 8);
	const bool bit = ps <= st->lower;
	if (bit) {
		st->lower -= ps;
		st->upper -= ps;
	} else {
		st->upper = ps;
	}
	const uint32_t i = bit_clz32((uint32_t)(st->upper << 16));
	if (i) {
		// renormalize bounds
		const uint32_t bits = bitstrm_msb_peek_high25(&st->bs);
		st->upper <<= i;
		st->lower = (uint16_t)(
			(unsigned)st->lower << i | bits >> 1 >> (31 - i)
		);
		bitstrm_seek(&st->bs, i);
	}
	return bit;
}

static uint8_t arithmetic_decode_num(struct arith_state *st,
const enum arith_code c) {
	unsigned num = 0xff;
	for (uint8_t i = 0; i < 8; ++i) {
		const bool exit = arithmetic_decode_bit(st, c + i);
		if (exit) {
			num >>= 8 - i;
			for (uint8_t k = 0; k < i; ++k) {
				num += (unsigned)arithmetic_decode_bit(st, c+8+k) << k;
			}
			break;
		}
	}
	return (uint8_t)num;
}

/* For reference only */
static int arithmetic_get_num_reference(uint8_t maxval, int bef, uint8_t num) {
	/* 0xff >= num >= 0x00, maxval >= bef >= 0 */
	if (bef > maxval/2) {
		if (num > maxval*2 - bef*2) {
			return maxval - num;
		}
	} else {
		if (num > bef*2) {
			return num;
		}
	}
	if (num & 1) {
		return bef + num/2 + 1;
	}
	return bef - num/2;
}

static int arithmetic_get_num(struct arith_state *st,
const enum arith_code c, const uint8_t maxval, int bef) {
	uint8_t num = arithmetic_decode_num(st, c);
	if (false) {
		return arithmetic_get_num_reference(maxval, bef, num);
	}
	const int dist = bef*2 - maxval;
	const int mnum = -num;
	const int mn = maxval + mnum;
	if (abs(dist) > mn) {
		return dist < 0 ? num : mn;
	}
	bool sign = num & 1;
	return bef + (sign ? num : mnum)/2 + sign;
}

static uint8_t clr_add(const uint32_t a, const uint32_t b,
const uint8_t shl, const uint8_t max) {
	uint32_t r = ((a >> shl) & max) + ((b >> shl) & max);
	return (uint8_t)(r >> 1);
}

static uint32_t pic2_arith_read_color(struct arith_state *st, const ptrdiff_t x,
const uint32_t left, const uint8_t depth, const uint32_t opaque,
const void *prev_row) {
	const uint32_t up = depth > 5
		? ((uint32_t *)prev_row)[x]
		: ((uint16_t *)prev_row)[x];
	const uint32_t key = (0x1c0 & (up >> (depth*3 - 9)))
		| (0x38 & (up >> (depth*2 - 6)))
		| (0x7 & (up >> (depth - 3)));

	const uint8_t maxval = 0xff >> (8 - depth);
	const uint8_t cache_mask = 0x1f;

	uint32_t cc;
	uint8_t tgt;
	if (arithmetic_decode_bit(st, st->cache_code)) {
		st->cache_code = arith_cache_miss;
		uint8_t r = clr_add(up, left, depth*2, maxval);
		uint8_t g = clr_add(up, left, depth, maxval);
		uint8_t b = clr_add(up, left, 0, maxval);

		int ng = arithmetic_get_num(st, arith_green, maxval, g);
		int nr = arithmetic_get_num(st, arith_red, maxval, r + ng - g);
		int nb = arithmetic_get_num(st, arith_blue, maxval, b + ng - g);

		cc = (uint32_t)(nr << depth*2 | ng << depth | nb);
		cc |= ((uint32_t)0 - (cc != opaque)) << depth*3;

		tgt = (st->cache_last[key] - 1) & cache_mask;
		st->cache_last[key] = tgt;
	} else {
		st->cache_code = arith_cache_hit;

		const uint8_t num = arithmetic_decode_num(st, arith_cache_shuf);
		tgt = st->cache_last[key];
		uint8_t c1 = (tgt + num) & cache_mask;
		uint8_t c2 = (tgt + num/2) & cache_mask;

		cc = st->cache->c[key][c1];
		st->cache->c[key][c1] = st->cache->c[key][c2];
		st->cache->c[key][c2] = st->cache->c[key][tgt];
	}
	st->cache->c[key][tgt] = cc;
	return cc;
}

static void expand_chain(struct arith_state *st, const ptrdiff_t x,
uint32_t cc, const enum arith_code v) {
	if (!arithmetic_decode_bit(st, v)) {
		uint8_t i = 1;
		while (i < 5 && !arithmetic_decode_bit(st, v + i)) {
			++i;
		}
		enum arith_code next = arith_chain_base + 6u*i;
		ptrdiff_t off = i/2 * (i & 1 ? 1 : -1);
		st->row_next[x + off] = cc;
		st->pos_next[x + off] |= (uint16_t)(next << PIC2_CHAIN_SH);
	}
}

static struct wu_st pic2_arithmetic_decoder(const struct pic2_image *b,
struct wuimg *img) {
	struct arith_state st;
	const size_t prob_table_size = sizeof(*st.prob)*128;
	const size_t strm_start = prob_table_size + 2;
	if (b->data.len <= strm_start) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	if (!arith_state_alloc(&st, img->w)) {
		return WUERR_HERE(wu_alloc_error);
	}
	for (size_t i = 0; i < ARRAY_LEN(st.prob); ++i) {
		// make probs >= 2 to save some operations in decode_bit()
		st.prob[i] = (uint16_t)umax(buf_endian16b(b->data.ptr + i*2), 2);
	}
	st.upper = 0xffff;
	st.lower = buf_endian16b(b->data.ptr + prob_table_size);
	memset(st.cache_last, 0, sizeof(st.cache_last));
	st.cache_code = arith_cache_miss;

	bitstrm_from_bytes(&st.bs, b->data.ptr + strm_start,
		b->data.len - strm_start);

	const size_t stride = img->w * (b->depth > 5 ? 4 : 2);
	uint32_t cc = 0;
	const void *prev_row = img->data;
	for (size_t y = 0; y < img->h; ++y) {
		void *dst = img->data + y*stride;
		for (ptrdiff_t x = 0; x < (ptrdiff_t)img->w; ++x) {
			enum arith_code code = st.pos_cur[x] >> PIC2_CHAIN_SH;
			if (code) {
				cc = st.row_cur[x];
			} else if (arithmetic_decode_bit(&st, st.pos_cur[x] & 0xf)) {
				++st.pos_cur[x+1];
				++st.pos_cur[x+2];
				++st.pos_next[x-1];
				++st.pos_next[x];
				++st.pos_next[x+1];
				++st.pos_next2[x-1];
				++st.pos_next2[x];
				++st.pos_next2[x+1];
				cc = pic2_arith_read_color(&st, x, cc, b->depth,
					b->opaque, prev_row);
				code = arith_chain_base;
			}
			if (code && y + 1 < img->h) {
				expand_chain(&st, x, cc, code);
			}
			if (b->depth > 5) {
				((uint32_t *)dst)[x] = cc;
			} else {
				((uint16_t *)dst)[x] = (uint16_t)cc;
			}
			st.pos_cur[x] = 0;
			st.row_cur[x] = 0;
		}
		arith_state_cycle(&st);
		prev_row = dst;
	}
	free(st.v);
	return WU_OK;
}

struct wu_st pic2_decode(const struct pic2_block *block, struct wuimg *img) {
	return pic2_arithmetic_decoder(&block->u.image, img);
}

struct wu_st pic2_set_image(const struct pic2_desc *desc,
const struct pic2_block *block, struct wuimg *img) {
	const struct pic2_image *b = &block->u.image;
	if (b->depth == 8) {
		img->channels = 4;
		img->bitdepth = 8;
		img->layout = which_end() == big_endian
			? pix_argb : pix_bgra;
	} else {
		img->channels = 1;
		img->bitdepth = (b->depth > 5) ? 32 : 16;
		img->layout = pix_bgra;
		struct bitfield *bf = wuimg_bitfield_from_id(img,
			(b->depth * 0x111u) | 0x1000u);
		if (!bf) {
			return WUERR_HERE(wu_alloc_error);
		}
	}
	img->w = b->w;
	img->h = b->h;
	img->alpha = alpha_key;
	// Presumably the same as with PIC
	wuimg_aspect_ratio(img, desc->y_aspect, desc->x_aspect);
	return WU_OK;
}

static uint32_t repack_opaque(uint32_t c, const uint8_t depth, const bool flag) {
	if (depth == 5) { // Stored as 5551 GRB
		uint32_t g = c >> 11;
		uint32_t r = (c >> 6) & 0x1f;
		uint32_t b = (c >> 1) & 0x1f;
		c = r << 10 | g << 5 | b;
	}
	// Make comparisons against this color false when flag is unset
	return c | (uint32_t)(!flag << 24);
}

static struct wu_st pic2_read_p2ss(struct pic2_desc *desc,
struct pic2_image *block, const struct wuptr data) {
	/* Image block struct (after id and size):
		Offset  Type    Name
		0       u16     Flags
		2       u16     ImageWidth
		4       u16     ImageHeight
		6       u16     XOffset     // Position within canvas?
		8       u16     YOffset
		10      u32     OpaqueColor // [*]
		14      u32     Reserved
		18      u8      Data[]

	 * [*] Color value that must be rendered as black when Flags & 1 == 1
	*/
	const size_t hsize = 18;
	if (data.len > hsize) {
		const uint16_t flags = buf_endian16b(data.ptr);
		const uint32_t reserved = buf_endian32b(data.ptr + 14);
		if (!reserved && flags <= 1) {
			*block = (struct pic2_image) {
				.depth = desc->depth,
				.w = buf_endian16b(data.ptr + 2),
				.h = buf_endian16b(data.ptr + 4),
				.x = buf_endian16b(data.ptr + 6),
				.y = buf_endian16b(data.ptr + 8),
				.opaque = repack_opaque(
					buf_endian32b(data.ptr + 10),
					desc->depth, flags),
				.data = (struct wuptr) {
					.ptr = data.ptr + hsize,
					.len = data.len - hsize,
				},
			};
			return WU_OK;
		}
		return wuerr(wu_invalid_header,
			"reserved field or unknown flags set");
	}
	return wuerr(wu_invalid_header, "image block is too small");
}

struct wu_st pic2_next_block(struct pic2_desc *desc, struct pic2_block *block) {
	/* Block struct header:
		Offset  Type    Name
		0       u8      ID[4]
		4       u32     BlockSize
		8
	*/
	const uint32_t hhsize = 8;
	const uint8_t *header_header = mp_slice(&desc->mp, hhsize);
	if (header_header) {
		block->id = buf_endian32b(header_header);
		block->is_image = false;
		if (block->id == pic2_end) {
			return WU_NO_CHANGE;
		}
		uint32_t size = buf_endian32b(header_header + 4);
		if (size < hhsize) {
			return wuerr(wu_unexpected_eof, "block size < 8");
		}
		size -= hhsize;
		struct wuptr data = mp_avail(&desc->mp, size);
		switch (block->id) {
		case pic2_p2ss:
			block->is_image = true;
			return pic2_read_p2ss(desc, &block->u.image, data);
		case pic2_pdpi:
			if (data.len == 2) {
				block->u.dpi = buf_endian16b(data.ptr);
				return WU_OK;
			}
			return wuerr(wu_invalid_header,
				"PDPI block with size != 10");
		default:
			return wuerr(wu_samples_wanted, "unknown PIC2 block");
		}
	}
	return WUERR_HERE(wu_unexpected_eof);
}

static struct wuptr text_field(const uint8_t *src, size_t len) {
	return wuptr_trim_end(wuptr_mem(src, len), ' ');
}

struct wu_st pic2_parse(struct pic2_desc *desc, const struct wuptr mem) {
	/* PIC2 format (after magic bytes):
		Offset  Type    Name
		0       u8      Magic[4]
		4       u8      Name[18]
		22      u8      Subtitle[8]
		30      u8      CRLF[2]      // 0x0d 0x0a
		32      u8      Title[30]
		62      u8      CRLF[2]
		64      u8      Saver[30]
		94      u8      CRLF[2]
		96      u8      EOF[2]       // 0x1a 0x00
		98      u16     HasPalette
		100     u16     ImageNumber  // [*]
		102     u32     CreationDate // Seconds since UNIX epoch
		106     u32     HeaderSize
		110     u16     Depth
		112     u16     XAspect
		114     u16     YAspect
		116     u16     CanvasWidth
		118     u16     CanvasHeight
		120     u32     Reserved
		124

	 * Afterwards, if HasPalette == 1:
		Offset  Type    Name
		0       u8      Depth
		1       u16     NColors
		3       u8      RGB[3][NColors]

	 * The rest of the header up to HeaderSize is a nul-delimited comment.

	 * [*] May indicate the file is part of a sequence, NOT that the file
	 *     contains this many images.
	*/
	*desc = (struct pic2_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t sig[4] = {'P', '2', 'D', 'T'};
	const uint8_t *head = mp_slice(&desc->mp, 124);
	if (!head) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(head, sig, sizeof(sig))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	desc->text = wuptr_mem(head + 4, 94);
	desc->name = text_field(head + 4, 18);
	desc->subtitle = text_field(head + 22, 8);
	desc->title = text_field(head + 32, 30);
	desc->saver = text_field(head + 64, 30);

	const uint8_t crlf[] = {0x0d, 0x0a};
	const uint8_t eof[] = {0x1a, 0x00};

	const uint8_t crlf_off[3] = {30, 62, 94};
	for (size_t i = 0; i < ARRAY_LEN(crlf_off); ++i) {
		if (memcmp(head + crlf_off[i], crlf, sizeof(crlf))) {
			return wuerr(wu_invalid_header,
				"text lines not terminated by CRLF");
		}
	}
	if (memcmp(head + 96, eof, sizeof(eof))) {
		return wuerr(wu_invalid_header,
			"text area not terminated by EOF character");
	}

	const uint16_t depth = buf_endian16b(head + 110);
	if (depth > 24) {
		return wuerr(wu_invalid_header, "depth > 24");
	} else if (depth < 9 || depth % 3) {
		return wuerr(wu_samples_wanted,
			"depth < 9 or not divisible by 3");
	}

	desc->depth = (uint8_t)(depth / 3);
	desc->image_number = buf_endian16b(head + 100);
	desc->created = buf_endian32b(head + 102);
	desc->x_aspect = buf_endian16b(head + 112);
	desc->y_aspect = buf_endian16b(head + 114);
	desc->w = buf_endian16b(head + 116);
	desc->h = buf_endian16b(head + 118);

	const uint16_t has_palette = buf_endian16b(head + 98);
	if (has_palette) {
		return wuerr(wu_samples_wanted, "paletted image");
	}

	size_t size = buf_endian32b(head + 106);
	if (size < desc->mp.pos + 1) { // Trailing nul is required
		return wuerr(wu_invalid_header,
			"header size less than minimum required");
	}
	size -= desc->mp.pos;
	const uint8_t *comment = mp_slice(&desc->mp, size);
	if (!comment) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	desc->comment = wuptr_mem(comment, size - 1);
	desc->start = desc->mp.pos;
	return WU_OK;
}
