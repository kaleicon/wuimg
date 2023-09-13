// SPDX-License-Identifier: 0BSD
#include "pic.h"
#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"

/* Based on
https://mooncore.eu/bunny/txt/picfmt_e.txt
 * with some studying of
https://github.com/DavidGriffith/xv/blob/master/xvpic.c

 * TODO:
 * · Test with 12-bit files
 * · Test with files of type Mac
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

void pic_cleanup(struct pic_desc *desc) {
	wustr_free(&desc->comm);
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

static void init_cache(struct pic_cache *cache) {
	cache->first = 0;
	for (size_t i = 0; i < ARRAY_LEN(cache->n); ++i) {
		cache->n[i] = (struct pic_cache_node) {
			.next = (i + 1) & 0x7f,
			.prev = (i - 1) & 0x7f,
		};
	}
}

static void read_cached_color(uint8_t *restrict dst, struct pic_cache *cache,
struct bitstrm *bs, const uint8_t ch) {
	/* Read an index from the stream, and if it's different from the
	 * current first node, make it the new first. */
	const uint8_t idx = (uint8_t)bitstrm_msb_adv(bs, 7);
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

static void cache_add_color(const uint8_t *restrict dst,
struct pic_cache *cache, const uint8_t ch) {
	cache->first = cache->n[cache->first].next;
	memcpy(&cache->n[cache->first].val, dst, ch);
}

static void read_grb(uint8_t dst[static 3], const struct pic_bits *bits,
struct bitstrm *bs) {
	uint32_t grb[3];
	for (size_t i = 0; i < ARRAY_LEN(bits->grb); ++i) {
		grb[i] = bitstrm_msb_adv(bs, bits->grb[i]);
	};
	const uint32_t s = bits->s ? bitstrm_msb_adv(bs, bits->s) : 0;
	for (size_t i = 0; i < ARRAY_LEN(bits->grb); ++i) {
		dst[i] = (uint8_t)(
			(((grb[i] << bits->s) | s) * bits->mul[i]) >> 8
		);
	};
}

static void read_color(uint8_t *restrict dst, const struct pic_desc *desc,
struct bitstrm *bs, const uint8_t ch) {
	switch (ch) {
	case 3: // 24-bits, 16-bits (intensity), pack_655
		read_grb(dst, &desc->bits, bs);
		break;
	case 2: // Tiled, 15-bits (pack_x555), 12-bits (pack_x444)
		*(uint16_t *)dst = (uint16_t)bitstrm_msb_adv(bs, desc->depth);
		break;
	case 1: // Paletted
		dst[0] = (uint8_t)bitstrm_msb_adv(bs, desc->bits.grb[0]);
		break;
	}
}

static void read_img_color(uint8_t dst[static 3], const struct pic_desc *desc,
struct bitstrm *bs, struct pic_cache *cache, const uint8_t ch) {
	if (cache) {
		if (bitstrm_msb_next(bs)) {
			read_cached_color(dst, cache, bs, ch);
		} else {
			read_color(dst, desc, bs, ch);
			cache_add_color(dst, cache, ch);
		}
	} else {
		read_color(dst, desc, bs, ch);
	}
}

static void chain_expand(uint8_t *restrict ptr, struct bitstrm *bs,
const uint8_t ch, int x, int y, const int w, const int limit, uint8_t *mask) {
	/* Replicates the current color downwards, with a variable x offset
		0b11    +1x
		0b10     0x
		0b01    -1x
		0b000    stop
		0b0011  +2x
		0b0010  -2x
	*/
	const int src = y * w + x;
	for (;;) {
		switch (bitstrm_msb_adv(bs, 2)) {
		case 3: ++x; break;
		case 2: break;
		case 1: --x; break;
		case 0:
			if (bitstrm_msb_next(bs)) {
				if (bitstrm_msb_next(bs)) {
					x += 2;
				} else {
					x -= 2;
				}
			} else {
				return;
			}
			break;
		}
		++y;
		const int dst = y * w + iclamp(x, 0, w);
		if (dst >= limit) {
			return;
		}
		memcpy(ptr + dst*ch, ptr + src*ch, ch);
		mask[dst/8] |= 1 << (dst%8);
	}
}

static uint32_t read_len_code(struct bitstrm *bs) {
	/* Variable int coding, starting at 1
		Code    Range
		0x      1-2
		10xx    3-6
		110xx   7-14
		1110xxx 15-30
	 * And so on, and so on
	*/
	unsigned b = 1;
	while (b < 29 && bitstrm_msb_next(bs)) {
		++b;
	}
	return bit_set32(b) + bitstrm_msb_adv(bs, b);
}

static bool decode_data(const struct pic_desc *desc, uint8_t *restrict dst,
const int w, const int limit, const uint8_t ch, struct bitstrm *bs,
uint8_t *mask, struct pic_cache *cache) {
	for (int i = -1; bs->pos < bs->len;) {
		i += (int)read_len_code(bs);
		if (i < limit) {
			read_img_color(dst + i*ch, desc, bs, cache, ch);
			mask[i/8] |= 1 << (i%8);
			if (bitstrm_msb_next(bs)) {
				const int x = i % w;
				const int y = i / w;
				chain_expand(dst, bs, ch, x, y, w, limit, mask);
			}
		} else {
			break;
		}
	}

	for (int i = 0, src = 0; i < limit; ++i) {
		const bool set = (mask[i/8] >> (i%8)) & 1;
		if (set) {
			src = i;
		} else {
			memcpy(dst + i*ch, dst + src*ch, ch);
		}
	}
	return true;
}

static uint8_t get_pix_size(const struct pic_desc *desc,
const struct wuimg *img) {
	if (desc->tiled || img->bitdepth != 8) {
		return 2;
	} else if (img->channels == 1) {
		return 1;
	}
	return 3;
}

size_t pic_decode(const struct pic_desc *desc, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}

	const size_t comp_size = file_remaining(desc->ifp);
	if (!comp_size) {
		return 0;
	}

	const size_t w = img->w;
	const size_t h = (desc->tiled ? img->h/2 : img->h);
	const size_t limit = w*h;
	const size_t mask_size = strip_base(limit, 1);

	const bool use_cache = desc->depth > 8 && desc->type != pic_type_mac;

	/* Allocate a single buffer with the data at the start and the other
	 * structures at the end to serve as padding. */
	struct pic_cache *cache = NULL;
	const size_t pad = zumax(8, mask_size + use_cache * sizeof(*cache));
	uint8_t *data = malloc(comp_size + pad);
	bool ok = false;
	if (data) {
		const size_t read = fread(data, 1, comp_size, desc->ifp);
		if (read) {
			struct bitstrm bs = bitstrm_from_bytes(data, read);

			uint8_t *mask = data + read;
			memset(mask, 0, mask_size);

			if (use_cache) {
				cache = (struct pic_cache *)(mask + mask_size);
				init_cache(cache);
			}

			const uint8_t ch = get_pix_size(desc, img);
			ok = decode_data(desc, img->data, (int)w, (int)limit,
				ch, &bs, mask, cache);
		}
		free(data);
	}
	return ok;
}

static bool load_pal(const struct pic_desc *desc, struct wuimg *img) {
	const struct pic_bits *b = &desc->bits;
	const uint8_t pal_depth = (uint8_t)(b->grb[0] + b->grb[1] + b->grb[2]
		+ b->s);
	const size_t entries = 1 << desc->depth;
	const size_t len = strip_base(entries, pal_depth);
	uint8_t *buf = (uint8_t *)(img->u.palette + 1) - len;
	if (!fread(buf, len, 1, desc->ifp)) {
		return false;
	}

	struct bitstrm bs = bitstrm_from_bytes(buf, len);
	struct pix_rgba8 *pal = img->u.palette->color;
	for (size_t i = 0; i < entries; ++i) {
		read_grb((uint8_t *)(pal + i), b, &bs);
		pal[i].a = 0xff;
	}
	return true;
}

static void calc_mul(struct pic_bits *bits, const uint8_t shared) {
	bits->s = shared;
	for (size_t i = 0; i < ARRAY_LEN(bits->grb); ++i) {
		const uint32_t depth = bits->grb[i] + bits->s;
		const uint32_t maxval = bit_set32(depth);
		bits->mul[i] = (uint16_t)((UCHAR_MAX << 8) / maxval + 1);
	}
}

static bool extra_fields(uint8_t *restrict buf, struct pic_desc *desc,
struct wuimg *img, const bool rgb_bits) {
	if (!fread(buf, 6 + rgb_bits, 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	switch (desc->mode) {
	case 0x00: case 0x0f: break;
	case 0x01:
		wuimg_aspect_ratio(img, 4, 3);
		break;
	default:
		return wu_invalid_header;
	}
	desc->x = (int16_t)buf_endian16(buf, big_endian);
	desc->y = (int16_t)buf_endian16(buf + 2, big_endian);
	const uint8_t num = buf[4];
	const uint8_t den = buf[5];
	if (num && den) {
		wuimg_aspect_ratio(img, num, den);
	} else if (desc->mode == 0x0f) {
		return wu_invalid_header;
	}
	return wu_ok;
}

enum wu_error pic_parse(struct pic_desc *desc, struct wuimg *img) {
	/* PIC header (after magic bytes):
		Offset  Size    Name
		0       u8      Comment[]     // 0x1a then 0x00 terminated
		...
		+0      u8      Reserved      // 0x00
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

	if (!file_read_pi_comm(&desc->comm, desc->ifp)) {
		return wu_unexpected_eof;
	}

	uint8_t buf[8];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	if (buf[0]) {
		return wu_invalid_header;
	}
	desc->type = buf[1] & 0xf;
	desc->mode = buf[1] >> 4;
	const uint16_t bd = buf_endian16(buf + 2, big_endian);
	uint32_t w = buf_endian16(buf + 4, big_endian);
	uint32_t h = buf_endian16(buf + 6, big_endian);

	img->layout = pix_grba;
	img->alpha = alpha_ignore;

	uint8_t uni_bits = 0;
	uint8_t shared_bits = 0;

	switch (desc->type) {
	enum wu_error st;
	case pic_type_x68k:
		if (desc->mode) {
			return wu_invalid_header;
		}
		uni_bits = 5;
		switch (bd) {
		case 16:
			shared_bits = 1;
			// fallthrough
		case 15:
			wuimg_aspect_ratio(img, 4, 3);
			break;
		case 8:
			wuimg_aspect_ratio(img, 4, 3);
			// fallthrough
		case 4:
			shared_bits = 1;
			if (!wuimg_palette_init(img)) {
				return wu_alloc_error;
			}
			break;
		default:
			return wu_invalid_header;
		}
		break;
	case pic_type_pc_88va:
		switch (w) {
		case 320:
			switch (h) {
			case 200: case 400: case 408: break;
			default: return wu_invalid_header;
			}
			break;
		case 640:
			switch (h) {
			case 200: case 204: case 400: break;
			default: return wu_invalid_header;
			}
			break;
		default: return wu_invalid_header;
		}
		desc->tiled = desc->mode & 0x02;
		if (desc->tiled) {
			if (bd != 16) {
				return wu_invalid_header;
			}
			h *= 2;
			img->attr = pix_pack_332;
		}
		/* According to the docs, images on the PC-88VA always cover
		 * the whole screen, so the image ratio is whatever is needed
		 * to stretch it to 640x400, or 320x400 if the low bit in mode
		 * is set. */
		const double hr = (desc->mode & 0x01) ? 320.0 : 640.0;
		img->ratio = (float)((w / hr) / (h / 400.0));
		switch (bd) {
		case 8:
			desc->bits = (struct pic_bits) {
				.grb = {3,3,2},
			};
			img->attr = pix_pack_332;
			break;
		case 12:
			uni_bits = 4;
			break;
		case 16:
			desc->bits = (struct pic_bits) {
				.grb = {6,5,5},
			};
			break;
		default:
			return wu_invalid_header;
		}
		break;
	case pic_type_fm_towns:
		if (bd != 15) {
			return wu_invalid_header;
		}
		uni_bits = 5;
		/* All the FM-TOWNS files I've found include extra header data
		 * like 'Generic' does, with mode 0. The docs don't say
		 * anything about this, so hopefully what follows is correct. */
		switch (desc->mode) {
		case 0x00:
			st = extra_fields(buf, desc, img, false);
			if (st != wu_ok) {
				return st;
			}
			break;
		case 0x05: case 0x0c: break;
		default: return wu_invalid_header;
		}
		break;
	case pic_type_mac:
		if (bd != 15) {
			return wu_invalid_header;
		}
		uni_bits = 5;
		img->layout = pix_rgba;
		break;
	case pic_type_generic:
		st = extra_fields(buf, desc, img, false);
		if (st != wu_ok) {
			return st;
		}

		switch (bd) {
		case 4: case 8:
			uni_bits = buf[6];
			if (!uni_bits || uni_bits > 8) {
				return wu_invalid_header;
			} else if (!wuimg_palette_init(img)) {
				return wu_alloc_error;
			}
			break;
		case 12:
			uni_bits = 4;
			break;
		case 15:
			uni_bits = 5;
			break;
		case 16:
			uni_bits = 5;
			shared_bits = 1;
			break;
		case 24:
			uni_bits = 8;
			break;
		case 32:
			return wu_unsupported_feature;
		default:
			return wu_invalid_header;
		}
		break;
	default:
		return wu_invalid_header;
	}

	desc->depth = (uint8_t)bd;
	if (img->attr == pix_pack_332) {
		img->channels = 1;
		img->bitdepth = 8;
	} else if (desc->depth == 12) {
		img->channels = 4;
		img->bitdepth = 4;
	} else if (desc->depth == 15) {
		img->channels = 1;
		img->bitdepth = 16;
		img->attr = pix_pack_1555;
		img->layout = PIX_LAYOUT_PACK(1, 2, 0, 3); // why?
	} else {
		if (uni_bits) {
			for (size_t i = 0; i < ARRAY_LEN(desc->bits.grb); ++i) {
				desc->bits.grb[i] = uni_bits;
			}
		}
		calc_mul(&desc->bits, shared_bits);
		if (img->mode == image_mode_palette) {
			if (!load_pal(desc, img)) {
				return wu_unexpected_eof;
			}
			desc->bits.grb[0] = desc->depth;
		}
		img->channels = (img->mode == image_mode_palette) ? 1 : 3;
		img->bitdepth = 8;
	}
	img->w = w;
	img->h = h;
	return wuimg_verify(img);
}

enum wu_error pic_open(struct pic_desc *desc, FILE *ifp) {
	*desc = (struct pic_desc) {
		.ifp = ifp,
	};
	const unsigned char sig[] = {'P', 'I', 'C'};
	return fmt_sigcmp(sig, sizeof(sig), ifp);
}
