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
https://github.com/jasper-software/xv/blob/main/src/xvpic2.c
 * and
https://discmaster.textfiles.com/view/10329/MACPOWER-1996-09.ISO.7z/MACPOWER-1996-09.ISO/%E7%AC%AC%EF%BC%92%E7%89%B9%E9%9B%86%EF%BC%9A%E3%83%97%E3%83%A9%E3%82%B0%E3%82%A4%E3%83%B3%E5%A4%A7%E9%9B%86%E5%90%88/PIC2%20Save/p2load.c

 * Despite appearances, this format is surprisingly simple.

 * Due to a lack of samples, only arithmetic decoding with 24- and 15-bit
 * images is supported. xvpic2.c says any depth % 3 == 0 is possible, and in
 * theory, this decoder should be able to handle those too.

 * TODO: find samples for
 * - 16-bit, 8-bit, and paletted images
 * - RLE and raw images
 * - files with multiple images
*/

static const uint8_t CHAIN_SH = 8;

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

	void *dst_prev;

	struct bitstrm bs;
	uint16_t aa, dd;
	uint16_t prob[128];
	uint8_t cache_last[512];
	enum arith_code cache_code:8;
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
	const unsigned idx = (st->aa >> 8) | 0x80;
	const unsigned p = (idx * st->prob[c]) >> 8;
	const uint16_t ps = (uint16_t)(p | !p);
	const bool bit = st->dd >= ps;
	if (bit) {
		st->dd -= ps;
		st->aa -= ps;
	} else {
		st->aa = ps;
	}
	const uint16_t bits = (uint16_t)bitstrm_msb_peek_max25(&st->bs, 15);
	const uint32_t i = bit_clz32((uint32_t)st->aa << 16);
	st->aa <<= i;
	st->dd = (uint16_t)(st->dd << i | bits >> (15 - i));
	bitstrm_seek(&st->bs, i);
	return bit;
}

static uint8_t arithmetic_decode_num(struct arith_state *st,
const enum arith_code c) {
	uint32_t num = 0xff;
	for (uint8_t i = 0; i < 8; ++i) {
		const bool exit = arithmetic_decode_bit(st, c + i);
		if (exit) {
			num >>= 8 - i;
			for (uint8_t k = 0; k < i; ++k) {
				num += (uint32_t)
					arithmetic_decode_bit(st, c + 8 + k) << k;
			}
			break;
		}
	}
	return (uint8_t)num;
}

static int arithmetic_get_num(struct arith_state *st,
const enum arith_code c, const uint8_t maxval, int bef) {
	const uint8_t num = arithmetic_decode_num(st, c);
	bef *= 2;
	const int dist = bef - maxval;
	const int mn = maxval - num;
	if (abs(dist) > mn) {
		return dist < 0 ? num : mn;
	}
	/* Equivalent to
	if (num & 1) {
		return bef + num/2 + 1;
	}
	return bef - num/2;
	*/
	return (bef + ((num&1) ? num : -num) + (num & 1))/2;
}

static uint8_t clr_add(const uint32_t a, const uint32_t b,
const uint8_t shl, const uint8_t max) {
	uint32_t r = ((a >> shl) & max) + ((b >> shl) & max);
	return (uint8_t)(r >> 1);
}

static uint32_t read_color(struct arith_state *st, const ptrdiff_t x,
const uint32_t left, const uint8_t depth, const uint32_t opaque) {
	const uint32_t up = depth > 5
		? ((uint32_t *)st->dst_prev)[x]
		: ((uint16_t *)st->dst_prev)[x];
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
		cc |= ((cc != opaque) ? 0xffu : 0) << depth*3;

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
		int8_t off;
		uint8_t code;
		switch (i) {
		case 1: off = 0; code = arith_chain_center; break;
		case 2: off = -1; code = arith_chain_left; break;
		case 3: off = 1; code = arith_chain_right; break;
		case 4: off = -2; code = arith_chain_left2; break;
		case 5: off = 2; code = arith_chain_right2; break;
		}
		st->row_next[x + off] = cc;
		st->pos_next[x + off] |= code << CHAIN_SH;
	}
}

static size_t arithmetic_decoder(const struct pic2_image *b, struct wuimg *img) {
	struct arith_state st;
	const size_t strm_start = sizeof(st.prob) + 2;
	if (b->data.len <= strm_start) {
		return 0;
	}
	if (!arith_state_alloc(&st, img->w)) {
		return 0;
	}
	for (size_t i = 0; i < ARRAY_LEN(st.prob); ++i) {
		st.prob[i] = buf_endian16(b->data.ptr + i*2, big_endian);
	}
	bitstrm_from_bytes(&st.bs, b->data.ptr + strm_start,
		b->data.len - strm_start);
	st.aa = 0xffff;
	st.dd = buf_endian16(b->data.ptr + sizeof(st.prob), big_endian);
	memset(st.cache_last, 0, sizeof(st.cache_last));
	st.cache_code = arith_cache_miss;

	uint32_t cc = 0;
	st.dst_prev = img->data;
	for (size_t y = 0; y < img->h; ++y) {
		st.pos_next2[0] = 0;
		void *dst = img->data + y*img->w * (b->depth > 5 ? 4 : 2);
		for (ptrdiff_t x = 0; x < (ptrdiff_t)img->w; ++x) {
			st.pos_next2[x+1] = 0;

			const enum arith_code code = st.pos_cur[x] >> CHAIN_SH;
			if (code) {
				cc = st.row_cur[x];
				if (y + 1 < img->h) {
					expand_chain(&st, x, cc, code);
				}
			} else if (arithmetic_decode_bit(&st, (uint8_t)st.pos_cur[x])) {
				++st.pos_cur[x+1];
				++st.pos_cur[x+2];
				++st.pos_next[x-1];
				++st.pos_next[x];
				++st.pos_next[x+1];
				++st.pos_next2[x-1];
				++st.pos_next2[x];
				++st.pos_next2[x+1];
				cc = read_color(&st, x, cc, b->depth, b->opaque);
				if (y + 1 < img->h) {
					expand_chain(&st, x, cc, arith_chain_base);
				}
			}
			st.row_cur[x] = 0;
			if (b->depth > 5) {
				((uint32_t *)dst)[x] = cc;
			} else {
				((uint16_t *)dst)[x] = (uint16_t)cc;
			}
		}
		arith_state_cycle(&st);
		st.dst_prev = dst;
	}
	free(st.v);
	return img->w * img->h;
}

size_t pic2_decode(const struct pic2_block *block, struct wuimg *img) {
	if (wuimg_alloc_noverify(img)) {
		return arithmetic_decoder(&block->u.image, img);
	}
	return 0;
}

enum wu_error pic2_set_image(const struct pic2_desc *desc,
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
			return wu_alloc_error;
		}
	}
	img->w = b->w;
	img->h = b->h;
	img->alpha = alpha_key;
	// Presumably the same as with PIC
	wuimg_aspect_ratio(img, desc->y_aspect, desc->x_aspect);
	return wuimg_verify(img);
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

static enum wu_error read_image_block(struct pic2_desc *desc,
struct pic2_image *block, const uint32_t size) {
	/* Image block struct (after id):
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
	const uint32_t hsize = 26;
	if (size > hsize) {
		const uint8_t *header = mp_slice(&desc->mp, hsize - 8);
		if (header) {
			const uint16_t flags = buf_endian16(header, big_endian);
			const uint32_t reserved = buf_endian32(header + 14, big_endian);
			if (!reserved && flags <= 1) {
				*block = (struct pic2_image) {
					.depth = desc->depth,
					.w = buf_endian16(header + 2, big_endian),
					.h = buf_endian16(header + 4, big_endian),
					.x = buf_endian16(header + 6, big_endian),
					.y = buf_endian16(header + 8, big_endian),
					.opaque = repack_opaque(
						buf_endian32(header + 10, big_endian),
						desc->depth, flags),
					.data = mp_avail(&desc->mp, size - hsize),
				};
				return wu_ok;
			}
			return wu_invalid_header;
		}
	}
	return wu_unexpected_eof;
}

enum wu_error pic2_next_block(struct pic2_desc *desc, struct pic2_block *block) {
	/* Block struct header:
		Offset  Type    Name
		0       u8      ID[4]
		4       u32     BlockSize
		8
	*/
	const uint8_t *header_header = mp_slice(&desc->mp, 8);
	if (header_header) {
		block->id = buf_endian32(header_header, big_endian);
		block->is_image = false;
		const uint32_t size = buf_endian32(header_header + 4, big_endian);
		switch (block->id) {
		case pic2_end:
			return wu_no_change;
		case pic2_p2ss:
			block->is_image = true;
			return read_image_block(desc, &block->u.image, size);
		case pic2_pdpi:
			if (size == 10) {
				const uint8_t *d = mp_slice(&desc->mp, 2);
				if (d) {
					block->u.dpi = buf_endian16(d, big_endian);
					return wu_ok;
				}
				return wu_unexpected_eof;
			}
			return wu_invalid_header;
		default:
			return wu_samples_wanted;
		}
	}
	return wu_unexpected_eof;
}

static struct wuptr text_field(const uint8_t *src, size_t len) {
	return wuptr_trim_end(wuptr_mem(src, len), ' ');
}

enum wu_error pic2_parse(struct pic2_desc *desc) {
	/* PIC2 format (after magic bytes):
		Offset  Type    Name
		0       u8      Name[18]
		18      u8      Subtitle[8]
		26      u8      CRLF[2]      // 0x0d 0x0a
		28      u8      Title[30]
		58      u8      CRLF[2]
		60      u8      Saver[30]
		90      u8      CRLF[2]
		92      u8      EOF[2]       // 0x1a 0x00
		94      u16     HasPalette
		96      u16     ImageNumber  // [*]
		98      u32     CreationDate // Seconds since UNIX epoch
		102     u32     HeaderSize
		106     u16     Depth
		108     u16     XAspect
		110     u16     YAspect
		112     u16     CanvasWidth
		114     u16     CanvasHeight
		116     u32     Reserved
		120

	 * Afterwards, if HasPalette == 1:
		Offset  Type    Name
		0       u8      Depth
		1       u16     NColors
		3       u8      RGB[NColors*3]

	 * The rest of the header up to HeaderSize is a nul-delimited comment.

	 * [*] May indicate the file is part of a sequence, NOT that the file
	 *     contains this many images.
	*/
	const uint8_t *head = mp_slice(&desc->mp, 120);
	if (!head) {
		return wu_unexpected_eof;
	}

	desc->text = wuptr_mem(head, 92);
	desc->name = text_field(head, 18);
	desc->subtitle = text_field(head + 18, 8);
	desc->title = text_field(head + 28, 30);
	desc->saver = text_field(head + 60, 30);

	const uint8_t crlf[] = {0x0d, 0x0a};
	const uint8_t eof[] = {0x1a, 0x00};

	const uint8_t eol_off[3] = {26, 58, 90};
	for (size_t i = 0; i < ARRAY_LEN(eol_off); ++i) {
		if (memcmp(head + eol_off[i], crlf, sizeof(crlf))) {
			return wu_invalid_header;
		}
	}
	if (memcmp(head + 92, eof, sizeof(eof))) {
		return wu_invalid_header;
	}

	const uint16_t depth = buf_endian16(head + 106, big_endian);
	if (depth > 24) {
		return wu_invalid_header;
	} else if (depth < 9 || depth % 3) {
		return wu_samples_wanted;
	}

	desc->depth = (uint8_t)(depth / 3);
	desc->image_number = buf_endian16(head + 96, big_endian);
	desc->created = buf_endian32(head + 98, big_endian);
	desc->w = buf_endian16(head + 112, big_endian);
	desc->h = buf_endian16(head + 114, big_endian);
	desc->x_aspect = buf_endian16(head + 108, big_endian);
	desc->y_aspect = buf_endian16(head + 110, big_endian);

	const uint16_t has_palette = buf_endian16(head + 94, big_endian);
	if (has_palette) {
		return wu_samples_wanted;
	}

	size_t size = buf_endian32(head + 102, big_endian);
	if (size < desc->mp.pos + 1) { // Trailing nul is required
		return wu_invalid_header;
	}
	size -= desc->mp.pos;
	const uint8_t *comment = mp_slice(&desc->mp, size);
	if (!comment) {
		return wu_unexpected_eof;
	}

	desc->comment = wuptr_mem(comment, size - 1);
	desc->start = desc->mp.pos;
	return wu_ok;
}

enum wu_error pic2_init(struct pic2_desc *desc, const struct wuptr mem) {
	*desc = (struct pic2_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t sig[4] = {'P', '2', 'D', 'T'};
	return fmt_sigcmp_mem(sig, sizeof(sig), &desc->mp);
}
