// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/file.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"

#include "lib/pic.h"

/* Based on
https://mooncore.eu/bunny/txt/picfmt_e.txt
 * with some studying of
https://github.com/DavidGriffith/xv/blob/master/xvpic.c

 * This format has a lot of cases, but with some care it's manageable.

 * TODO:
 * - Find 24-bit files
 * - Find 12-bit files
 * - Find files of type Mac
*/

const char * pic_model_str(const enum pic_type type) {
	switch (type) {
	case pic_type_x68k: return "X68000";
	case pic_type_pc_88va: return "PC-88VA";
	case pic_type_fm_towns: return "FM-TOWNS";
	case pic_type_mac: return "Mac";
	case pic_type_generic: return "Generic";
	}
	return "???";
}

struct pic_cache {
	/* PIC's color cache algorithm works by using an structure that is
	 * indexed as an array but modified like a linked list.
	 * The list starts at cache.first, and is ordered from newest to oldest
	 * color. Each node points to the one newer (n.next) and older (n.prev)
	 * than it, with the first and last node pointing to each other to make
	 * the list a ring.
	 * This permits bringing previous colors to the beginning of the list
	 * without having to move any data, while keeping constant index times.
	 * Additionally, dropping the oldest color simply consists of
	 * overwriting the first.next element, no matter how tangled up the
	 * list may become. */
	uint8_t first;
	struct pic_cache_node {
		uint8_t next, prev;
		struct pix_rgb8 val;
	} n[128];
};

static void pic_init_cache(struct pic_cache *cache) {
	cache->first = 0;
	for (size_t i = 0; i < ARRAY_LEN(cache->n); ++i) {
		cache->n[i] = (struct pic_cache_node) {
			.next = (i + 1) & 0x7f,
			.prev = (i - 1) & 0x7f,
		};
	}
}

static void pic_read_cached_color(uint8_t *restrict dst,
struct pic_cache *cache, const uint8_t ch, const uint8_t idx) {
	/* Read an index from the stream, and if it's different from the
	 * current first node, make it the new first. */
	const uint8_t first = cache->first;
	struct pic_cache_node *n = cache->n;
	if (first != idx) {
		/* Make n[idx] the new first node.
		 * Assume we had four nodes, in order A B C D, with A as the
		 * newest and D the oldest.
		 * Assume that node C was selected. */

		// Make B and D point to each other so that C is out of the ring
		n[n[idx].next].prev = n[idx].prev;
		n[n[idx].prev].next = n[idx].next;

		// Make C point to A and D
		n[idx].next = n[first].next;
		n[idx].prev = first;

		// Make D and A point to C
		n[n[first].next].prev = idx;
		n[first].next = idx;

		// Order is now C A B D.
		cache->first = idx;
	}
	memcpy(dst, &n[idx].val, ch);
}

static void pic_cache_add_color(const uint8_t *restrict dst,
struct pic_cache *cache, const uint8_t ch) {
	cache->first = cache->n[cache->first].next;
	memcpy(&cache->n[cache->first].val, dst, ch);
}

static void pic_read_grb(uint8_t dst[static 3], const struct pic_bits *bits,
struct bitstrm *bs) {
	uint32_t b = bitstrm_msb_adv_max25(bs,
		(uint8_t)(bits->uni*3 + bits->shared));
	const bool s = b & bits->shared;
	for (uint32_t i = 0; i < 3; ++i) {
		const uint32_t off = bits->uni * (2 - i) + bits->shared;
		const uint32_t col = (b >> off) & bits->and;
		dst[i] = (uint8_t)(
			(((col << bits->shared) | s) * bits->mul) >> 8
		);
	}
}

static void pic_read_color(uint8_t *restrict dst, const struct pic_desc *desc,
struct bitstrm *bs, const uint8_t ch) {
	if (desc->bits.shared) { // 16-bits (intensity)
		pic_read_grb(dst, &desc->bits, bs);
	} else {
		const uint32_t b = bitstrm_msb_adv_max25(bs, desc->depth);
		switch (ch) {
		case 3: // 24-bits
			dst[0] = (uint8_t)(b >> 16);
			dst[1] = (uint8_t)(b >> 8);
			dst[2] = (uint8_t)b;
			break;
		case 2: // 16-bits (tiled 332, pack_655, pack_x555)
			// 12-bits (pack_x444)
			*(uint16_t *)dst = (uint16_t)b;
			break;
		case 1: // 8-bits (332), 8- or 4-bits (paletted)
			dst[0] = (uint8_t)b;
			break;
		}
	}
}

static void pic_read_img_color(uint8_t dst[static 3],
const struct pic_desc *desc, struct bitstrm *bs, struct pic_cache *cache,
const uint8_t ch) {
	if (cache) {
		const uint32_t b = bitstrm_msb_peek_max25(bs, 8);
		if (b & 0x80) {
			bitstrm_seek(bs, 8);
			pic_read_cached_color(dst, cache, ch, b & 0x7f);
		} else {
			bitstrm_seek(bs, 1);
			pic_read_color(dst, desc, bs, ch);
			pic_cache_add_color(dst, cache, ch);
		}
	} else {
		pic_read_color(dst, desc, bs, ch);
	}
}

static void pic_chain_expand(uint8_t *restrict ptr, struct bitstrm *bs,
const uint8_t ch, int x, int y, const int w, const int limit, uint8_t *mask) {
	/* Replicates the current color downwards, with a variable x offset
		11    +1x
		10     0x
		01    -1x
		000    stop
		0011  +2x
		0010  -2x
	*/
	const int src = y * w + x;
	for (;;) {
		uint8_t read = 2;
		switch (bitstrm_msb_peek_max25(bs, 4)) {
		case 0: case 1:
			bitstrm_seek(bs, 3);
			return;
		case 2:
			x -= 2; read = 4;
			break;
		case 3:
			x += 2; read = 4;
			break;
		case 4: case 5: case 6: case 7:
			x -= 1;
			break;
		case 12: case 13: case 14: case 15:
			x += 1;
			break;
		}
		bitstrm_seek(bs, read);
		++y;
		const int dst = y * w + x;
		if (dst < 0 || dst >= limit) {
			return;
		}
		memcpy(ptr + dst*ch, ptr + src*ch, ch);
		mask[dst/8] |= 1 << (dst%8);
	}
}

static uint32_t pic_read_len_code(struct bitstrm *bs) {
	/* Variable int coding, starting at 1
		Code    Range
		0x      1-2
		10xx    3-6
		110xx   7-14
		1110xxx 15-30
	 * And so on, and so on
	*/
	uint32_t bits = bitstrm_msb_peek_32(bs);
	const uint32_t z = bit_clo32(bits) + 1;
	bs->pos += z;
	const uint32_t val = bit_set32(z)
		+ (bitstrm_msb_peek_32(bs) >> (32 - z));
	bitstrm_seek(bs, z);
	return val;
}

static void pic_decode_data(const struct pic_desc *desc, uint8_t *restrict dst,
const int w, const int limit, const uint8_t ch, struct bitstrm *bs,
uint8_t *mask, struct pic_cache *cache) {
	int i = -1;
	while (!bs->eof) {
		i += (int)pic_read_len_code(bs);
		if ((unsigned)i >= (unsigned)limit) {
			break;
		}
		pic_read_img_color(dst + i*ch, desc, bs, cache, ch);
		mask[i/8] |= 1 << (i%8);
		if (bitstrm_msb_next(bs)) {
			const int x = i % w;
			const int y = i / w;
			pic_chain_expand(dst, bs, ch, x, y, w, limit, mask);
		}
	}

	i = 0;
	int src = 0;
	while (i < limit && !mask[i/8]) {
		i += 8;
	}
	while (i < limit) {
		if (mask[i/8] || i + 8 > limit) {
			for (int b = 0; b < 8 && i < limit; ++b) {
				const bool set = (mask[i/8] >> (i%8)) & 1;
				if (set) {
					src = i;
				} else {
					memcpy(dst + i*ch, dst + src*ch, ch);
				}
				++i;
			}
		} else {
			memwordset(dst + i*ch, dst + src*ch, ch, 8);
			i += 8;
		}
	}
}

static uint8_t pic_pix_size(const struct pic_desc *desc,
const struct wuimg *img) {
	if (desc->tiled || img->bitdepth != 8) {
		return 2;
	} else if (img->channels == 1) {
		return 1;
	}
	return 3;
}

struct wu_st pic_decode(const struct pic_desc *desc, struct wuimg *img) {
	struct bitstrm bs;
	bitstrm_from_wuptr(&bs, desc->data);

	const size_t w = img->w;
	const size_t h = (desc->tiled ? img->h/2 : img->h);
	const size_t limit = w*h;
	const size_t mask_size = strip_base(limit, 1);

	const bool use_cache = desc->depth > 8 && desc->type != pic_type_mac;

	struct pic_cache *cache = NULL;
	uint8_t *data = calloc(mask_size + use_cache * sizeof(*cache), 1);
	if (!data) {
		return wuerr(wu_alloc_error,
			"failed to allocated decoding buffer");
	}

	uint8_t *mask = data;
	if (use_cache) {
		cache = (struct pic_cache *)(data + mask_size);
		pic_init_cache(cache);
	}

	const uint8_t ch = pic_pix_size(desc, img);
	pic_decode_data(desc, img->data, (int)w, (int)limit,
		ch, &bs, mask, cache);
	free(data);
	return WU_OK;
}

static bool pic_load_pal(struct pic_desc *desc, struct palette *pal,
struct mparser *mp) {
	const struct pic_bits *b = &desc->bits;
	const uint8_t pal_depth = (uint8_t)(b->uni*3 + b->shared);
	const size_t entries = 1 << desc->depth;
	const size_t len = strip_base(entries, pal_depth);
	const uint8_t *buf = mp_slice(mp, len);
	if (buf) {
		struct bitstrm bs;
		bitstrm_from_bytes(&bs, buf, len);
		for (size_t i = 0; i < entries; ++i) {
			pic_read_grb((uint8_t *)(pal->color + i), b, &bs);
			pal->color[i].a = 0xff;
		}
	}
	return buf;
}

static void pic_set_bit_params(struct pic_bits *bits, const uint8_t uni,
const uint8_t shared) {
	const uint32_t maxval = bit_set32((uint32_t)(uni + shared));
	bits->mul = (uint16_t)((UCHAR_MAX << 8) / maxval + 1);
	bits->and = (uint8_t)bit_set32(uni);
	bits->shared = shared;
	bits->uni = uni;
}

static struct wu_st pic_parse_extra_fields(struct pic_desc *desc,
struct wuimg *img, struct mparser *mp) {
	const uint8_t *buf = mp_slice(mp, 6);
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	switch (desc->mode) {
	case 0x00: case 0x0f: break;
	case 0x01:
		wuimg_aspect_ratio(img, 4, 3);
		break;
	default:
		return wuerr(wu_invalid_header,
			"bad mode with extra fields");
	}
	desc->x = (int16_t)buf_endian16b(buf);
	desc->y = (int16_t)buf_endian16b(buf + 2);
	const uint8_t num = buf[4];
	const uint8_t den = buf[5];
	if (num && den) {
		wuimg_aspect_ratio(img, num, den);
	} else if (desc->mode == 0x0f) {
		return wuerr(wu_invalid_header,
			"mode 0x0f but no aspect ratio specified");
	}
	return WU_OK;
}

struct wu_st pic_parse(struct pic_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* PIC header (after magic bytes):
		Offset  Size    Name
		0       char    Magic[3]
		3       u8      Comment[]     // 0x1a terminated
		?       u8      Dummy[]       // 0x00 terminated
		...
		+0      u8      Reserved      // 0x00 (not the Dummy terminator)
		+1      u8      ModelBitfield
			0-3     Type
			4-7     Mode
		+2      u16     Bitdepth
		+4      u16     Width
		+6      u16     Height
		+8

	 * Extra fields when Type is 0xf (generic):
		+8      u16     XOffset
		+10     u16     YOffset
		+12     u8      AspectNum
		+13     u8      AspectDen
		+14

	 * Extra field when, additionally, Bitdepth is 4 or 8:
		+14     u8      RGBBits
		+15

	 * Afterwards comes the palette, which is stored at the native bitdepth
	 * and is bit-padded at the end so that the compressed data begins on a
	 * byte boundary.
	*/

	const unsigned char sig[] = {'P', 'I', 'C'};
	struct mparser mp = mp_wuptr(mem);
	const uint8_t *buf = mp_slice(&mp, sizeof(sig));
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(buf, sig, sizeof(sig))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	memset(desc, 0, sizeof(*desc));
	if (!mp_upto(&mp, &desc->comm, 0x1a)
	|| !mp_upto(&mp, &desc->dummy, 0x00)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	buf = mp_slice(&mp, 8);
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (buf[0]) {
		return wuerr(wu_invalid_header, "reserved byte != 0");
	}
	desc->type = buf[1] & 0xf;
	desc->mode = buf[1] >> 4;
	const uint16_t bd = buf_endian16b(buf + 2);
	uint32_t w = buf_endian16b(buf + 4);
	uint32_t h = buf_endian16b(buf + 6);

	img->layout = pix_grba;
	img->alpha = alpha_ignore;

	uint8_t uni_bits = 0;
	uint8_t shared_bits = 0;
	uint16_t bitfield = 0;

	switch (desc->type) {
	struct wu_st st;
	case pic_type_x68k:
		if (desc->mode) {
			return wuerr(wu_invalid_header,
				"mode byte set with pic type X68K");
		}
		uni_bits = 5;
		switch (bd) {
		case 16:
			shared_bits = 1;
			wuimg_aspect_ratio(img, 4, 3);
			break;
		case 15:
			bitfield = 0x555;
			wuimg_aspect_ratio(img, 4, 3);
			break;
		case 8:
			wuimg_aspect_ratio(img, 4, 3);
			// fallthrough
		case 4:
			shared_bits = 1;
			break;
		default:
			return wuerr(wu_invalid_header,
				"bad bitdepth for X68K pic");
		}
		break;
	case pic_type_pc_88va:
		switch (w) {
		case 320:
			switch (h) {
			case 200: case 400: case 408: break;
			default:
				return wuerr(wu_invalid_header,
					"bad height for 320 wide PC-88VA pic");
			}
			break;
		case 640:
			switch (h) {
			case 200: case 204: case 400: break;
			default:
				return wuerr(wu_invalid_header,
					"bad height for 640 wide PC-88VA pic");
			}
			break;
		default:
			return wuerr(wu_invalid_header,
				"bad width for PC-88VA pic");
		}
		switch (bd) {
		case 8:
			bitfield = 0x332;
			break;
		case 12: break;
		case 16:
			bitfield = 0x655;
			break;
		default:
			return wuerr(wu_invalid_header,
				"bad bitdepth for PC-88VA pic");
		}
		desc->tiled = desc->mode & 0x02;
		if (desc->tiled) {
			// Two 332-encoded pixels are packed into a 16bit word
			if (bd != 16) {
				return wuerr(wu_invalid_header,
					"bitdepth != 16 with tiled PC-88VA pic");
			}
			h *= 2;
			bitfield = 0x332;
		}
		/* According to the docs, images on the PC-88VA always cover
		 * the whole screen, so the image ratio is whatever is needed
		 * to stretch it to 640x400, or 320x400 if the low bit in mode
		 * is set. */
		const double hr = (desc->mode & 0x01) ? 320.0 : 640.0;
		img->ratio = (float)((w / hr) / (h / 400.0));
		break;
	case pic_type_fm_towns:
		if (bd != 15) {
			return wuerr(wu_invalid_header,
				"bitdepth != 15 with FM-TOWNS pic");
		}
		bitfield = 0x555;
		/* All the FM-TOWNS files I've found include extra header data
		 * like 'Generic' does, with mode 0. The docs don't say
		 * anything about this, so hopefully what follows is correct. */
		switch (desc->mode) {
		case 0x00:
			st = pic_parse_extra_fields(desc, img, &mp);
			if (!wu_isok(st)) {
				return st;
			}
			break;
		case 0x05: case 0x0c: break;
		default:
			return wuerr(wu_invalid_header,
				"bad mode for FM-TOWNS pic");
		}
		break;
	case pic_type_mac:
		if (bd != 15) {
			return wuerr(wu_invalid_header,
				"bitdepth != 15 with MAC pic");
		}
		bitfield = 0x555;
		img->layout = pix_rgba;
		break;
	case pic_type_generic:
		st = pic_parse_extra_fields(desc, img, &mp);
		if (!wu_isok(st)) {
			return st;
		}

		switch (bd) {
		case 4: case 8:
			;int bits = mp_next_char(&mp);
			if (bits == EOF) {
				return WUERR_HERE(wu_unexpected_eof);
			} else if (bits < 1 || bits > 8) {
				return wuerr(wu_invalid_header,
					"bits < 1 or bits > 8");
			}
			uni_bits = (uint8_t)bits;
			break;
		case 12: case 24:
			break;
		case 15:
			bitfield = 0x555;
			break;
		case 16:
			uni_bits = 5;
			shared_bits = 1;
			break;
		case 32:
			return wuerr(wu_samples_wanted,
				"32-bit generic pic type");
		default:
			return wuerr(wu_invalid_header,
				"bad bitdepth for generic pic");
		}
		break;
	default:
		return wuerr(wu_invalid_header,
			"bad pic type");
	}

	desc->depth = (uint8_t)bd;
	img->w = w;
	img->h = h;
	if (bitfield) {
		img->channels = 1;
		img->bitdepth = (bitfield == 0x332) ? 8 : 16;
		img->layout = pix_layout_mul(pix_bgra, img->layout);
		if (!wuimg_bitfield_from_id(img, bitfield)) {
			return WUERR_HERE(wu_alloc_error);
		}
	} else if (desc->depth == 12) {
		img->channels = 4;
		img->bitdepth = 4;
	} else {
		img->channels = (desc->depth > 8) ? 3 : 1;
		img->bitdepth = 8;
		if (uni_bits) {
			pic_set_bit_params(&desc->bits, uni_bits, shared_bits);
			if (desc->depth <= 8) {
				struct palette *pal = wuimg_palette_init(img);
				if (!pal) {
					return WUERR_HERE(wu_alloc_error);
				} else if (!pic_load_pal(desc, pal, &mp)) {
					return WUERR_HERE(wu_unexpected_eof);
				}
				desc->bits.shared = 0;
			}
		}
	}
	desc->data = mp_remaining(&mp);
	return WU_OK;
}
