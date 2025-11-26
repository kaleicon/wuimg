// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"

#include "lib/chunsoft.h"

/* SIR0 is a general purpose format used to load arbitrary data into DS memory.
 * Unfortunately, what kind of data it contains is not specified in the file.
 * It could be an image, a sprite, sequenced music, a 3D model, etc.
 * Fortunately, to save on space, AT6P is used to compress SIR0 files
 * containing images, while sprites are left uncompressed. Hence, we assume
 * that anything read directly from disk is sprite data.

 * SIR0 also has the nice property of specifying the location of every
 * data pointer in the file, making it obvious which fields are file offsets
 * and which are values.
 * Maybe that could work as a type fingerprint? Hmm...

https://projectpokemon.org/home/docs/mystery-dungeon-nds/sir0siro-format-r46/

 * FIXME: These files render pieces at a wrong offset, but everything is
 * being parsed correctly?
 * * akane_douyou_c.dat
 * * kubota_obie_c.dat

 * TODO: Get rid of top and left padding.
*/

void sir0_spr_cleanup(struct sir0_spr_desc *desc) {
	palette_unref(desc->pal);
}

static struct wu_st seek_anim(struct sir0_spr_desc *desc, const uint8_t i,
struct compost *afr, uint16_t *frames) {
	const uint32_t ptr = buf_endian32(desc->anim_ptrs + i*4, little_endian);
	if (!ptr) {
		return wuerr(wu_no_change, NULL);
	}
	mp_seek_set(&desc->mp, ptr);
	const uint8_t *hdr = mp_slice(&desc->mp, 12);
	if (hdr) {
		*afr = (struct compost) {
			.x = buf_endian16(hdr, little_endian),
			.y = buf_endian16(hdr+2, little_endian),
			.w = hdr[4],
			.h = hdr[5],
		};
		*frames = buf_endian16(hdr + 10, little_endian);
		return wuok();
	}
	return WUERR_HERE(wu_unexpected_eof);
}

static struct wu_st get_tile_info(struct sir0_spr_desc *desc, struct compost *fr,
uint16_t *off) {
	/* Sprite geom:
		Offset  Type    Name
		0       u16     Width
		2       u16     Height
		4       u16     X
		6       u16     Y
		8       u16     RasterOffset
		10
	*/
	const struct wuptr hdr = mp_avail(&desc->mp, 10);
	if (hdr.len > 4) {
		if (hdr.len >= 10) {
			*fr = (struct compost) {
				.w = buf_endian16(hdr.ptr, little_endian),
				.h = buf_endian16(hdr.ptr + 2, little_endian),
				.x = buf_endian16(hdr.ptr + 4, little_endian),
				.y = buf_endian16(hdr.ptr + 6, little_endian),
			};
			*off = buf_endian16(hdr.ptr + 8, little_endian);
			if (fr->w && fr->h) {
				return wuok();
			}
		}
		return wuerr(wu_no_change, NULL);
	}
	return wuerr(wu_unexpected_eof, "EOF while reading tile info");
}

struct wu_st sir0_spr_read_tile(struct sir0_spr_desc *desc, struct wuimg *img) {
	const size_t dims = img->w * img->h;
	const struct wuptr data = mp_avail(&desc->mp, dims);
	if (data.len < dims) {
		if (!wuimg_alloc_noverify(img)) {
			return WUERR_HERE(wu_alloc_error);
		}
		memcpy(img->data, data.ptr, data.len);
	} else {
		img->data = (uint8_t *)data.ptr;
		img->borrowed = true;
	}
	return wuerr_partial(data.len, dims);
}

static size_t partial_compost(uint8_t *restrict dst, const size_t stride,
const struct wuptr src, const struct compost *fr) {
	size_t d = fr->y*stride + fr->x;
	size_t s = 0;
	for (size_t i = 0; i < fr->h; ++i) {
		const size_t len = zumin(src.len - s, fr->w);
		const uint8_t *ptr = src.ptr + s;
		size_t p = 0;
		// Palette entry 0 is transparent
		while (p < len) {
			const uint8_t *color = memchk(ptr + p, 0, len - p);
			if (!color) {
				break;
			}
			p = (size_t)(color - ptr);
			const uint8_t *end = memchr(ptr + p, 0, len - p);
			if (!end) {
				end = ptr + len;
			}
			const size_t span = (size_t)(end - color);
			memcpy(dst + d + p, ptr + p, span);
			p += span;
		}
		s += len;
		d += stride;
	}
	return s;
}

struct wu_st sir0_spr_assemble_frame(struct sir0_spr_desc *desc,
struct wuimg *img, const uint8_t i, const uint16_t frame) {
	struct compost afr;
	uint16_t _frames;
	struct wu_st st = seek_anim(desc, i, &afr, &_frames);
	if (!wu_isok(st)) {
		return st;
	}

	mp_seek_cur(&desc->mp, frame*8);
	const uint8_t *hdr = mp_slice(&desc->mp, 8);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint16_t off = buf_endian16(hdr + 2, little_endian);
	const size_t dims = afr.w * afr.h;
	const struct wuptr tile = mp_avail_at(&desc->mp, desc->raster_off + off,
		dims);
	return wuerr_partial(partial_compost(img->data, img->w, tile, &afr), dims);
}

struct wu_st sir0_spr_assemble(struct sir0_spr_desc *desc, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return WUERR_HERE(wu_alloc_error);
	}

	size_t did = 0;
	size_t ought = 0;
	mp_seek_set(&desc->mp, desc->sprite_off);
	for (uint32_t i = 0; i < desc->nb_tiles; ++i) {
		struct compost fr;
		uint16_t off;
		get_tile_info(desc, &fr, &off);
		//fr.x -= desc->fr.x;
		//fr.y -= desc->fr.y;

		const size_t dims = fr.w * fr.h;
		const struct wuptr tile = mp_avail_at(&desc->mp,
			desc->raster_off + off, dims);
		did += partial_compost(img->data, img->w, tile, &fr);
		ought += dims;
	}
	return wuerr_partial(did, ought);
}

static struct wu_st set_img(struct sir0_spr_desc *desc, struct wuimg *img,
const struct compost *fr, const struct compost *afr, const uint16_t frames) {
	img->w = fr->w;
	img->h = fr->h;
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 5;
	wuimg_palette_set(img, palette_ref(desc->pal));
	struct wutree *tree = wuimg_get_metadata(img);
	if (tree) {
		tree_bud_leaf_u(tree, "X", fr->x);
		tree_bud_leaf_u(tree, "Y", fr->y);
	}
	if (frames) {
		if (!wuimg_frames_init(img, frames)) {
			return WUERR_HERE(wu_alloc_error);
		}
		for (uint16_t f = 0; f < frames; ++f) {
			const bool ok = wuimg_frame_set(img, f,
				afr->x, afr->y, afr->w, afr->h,
				4, 60, true);
			if (!ok) {
				return WUERR_HERE(wu_invalid_params);
			}
		}
	}
	return wuerr(wuimg_verify(img), NULL);
}

struct wu_st sir0_spr_set_tile(struct sir0_spr_desc *desc, struct wuimg *img,
const uint32_t i) {
	struct compost fr;
	uint16_t off;
	mp_seek_set(&desc->mp, desc->sprite_off + i*10);
	const struct wu_st st = get_tile_info(desc, &fr, &off);
	mp_seek_set(&desc->mp, desc->raster_off + off);
	return wu_isok(st)
		? set_img(desc, img, &fr, NULL, 0)
		: st;
}

struct wu_st sir0_spr_assemble_info(struct sir0_spr_desc *desc,
struct wuimg *img, const uint8_t i) {
	/* Anim struct:
		Offset  Type    Name
		0       u16     X
		2       u16     Y
		4       u8      Width
		5       u8      Height
		6       u8      WidthAgain?
		7       u8      HeightAgain?
		8       u16     Zero?
		10      u16     NbFrames
		12      frame   Frames[NbFrames]
	 * Frame struct:
		0       u16     ???              // Always 4 [*]
		2       u16     RasterOffset
		4       u16     AbsoluteOffset   // for what purpose
		6       u16     Zero?
		8
	 * [*] Maybe frame duration? 60/4 = 12fps, which looks correct.
	*/

	struct compost fr = desc->fr;
	struct compost afr;
	uint16_t frames = 0;
	const struct wu_st st = seek_anim(desc, i, &afr, &frames);
	if (wu_isok(st) && frames) {
		fr.x = zumin(fr.x, afr.x);
		fr.y = zumin(fr.y, afr.y);
		fr.w = zumax(fr.w, afr.x + afr.w);
		fr.h = zumax(fr.h, afr.y + afr.h);
	}
	return set_img(desc, img, &fr, &afr, frames);
}

static struct wu_st get_assemble_dims(struct sir0_spr_desc *desc) {
	struct compost fr;
	uint16_t _off;
	desc->nb_tiles = 0;
	desc->fr.x = SIZE_MAX;
	desc->fr.y = SIZE_MAX;
	struct wu_st st;

	mp_seek_set(&desc->mp, desc->sprite_off);
	while (wu_isok( (st = get_tile_info(desc, &fr, &_off)) )) {
		desc->fr.x = zumin(desc->fr.x, fr.x);
		desc->fr.y = zumin(desc->fr.y, fr.y);
		desc->fr.w = zumax(desc->fr.w, fr.x + fr.w);
		desc->fr.h = zumax(desc->fr.h, fr.y + fr.h);
		++desc->nb_tiles;
	}
	if (desc->nb_tiles) {
		return wuerr(wu_ok, st.msg);
	}
	st.st = (st.st == wu_no_change) ? wu_no_image_data : st.st;
	return st;
}

static struct wu_st get_pal(struct mparser *mp, struct palette *pal,
const uint32_t pal_off) {
	mp_seek_set(mp, pal_off);
	const size_t pal_len = 256;
	const uint8_t *p = mp_slice(mp, pal_len*2);
	if (!p) {
		return wuerr(wu_unexpected_eof, "EOF on palette");
	}
	for (size_t i = 0; i < pal_len; ++i) {
		const uint16_t c = buf_endian16(p + i*2, little_endian);
		pal->color[i] = (struct pix_rgba8) {
			.r = c & 0x1f,
			.g = (c >> 5) & 0x1f,
			.b = (c >> 10) & 0x1f,
			.a = i ? 0x1f : 0x00,
		};
	}
	return wuok();
}

static struct wu_st sprite_parse(struct sir0_spr_desc *desc) {
	/* Sprite header:
		Offset  Type    Name
		0       off     AnimNames[16] // Zeroed when unused
		64      off     RealHeader
		68      u8      Zero[116]
		184
	*/
	const uint8_t *hdr = mp_slice(&desc->mp, 184);
	if (!hdr) {
		return wuerr(wu_unexpected_eof, "EOF on sprite header");
	}

	uint8_t nb_anim = 0;
	while (nb_anim < 16 && buf_endian32(hdr + nb_anim*4, little_endian)) {
		++nb_anim;
	}
	desc->nb_anim = nb_anim;
	desc->nb_images = nb_anim ? nb_anim : 1;

	const uint32_t realheader = buf_endian32(hdr + 64, little_endian);
	mp_seek_set(&desc->mp, realheader);

	/* Real sprite header:
		Offset  Type    Name
		0       u32     RasterSize
		4       u32     ???
		8       u32     ???
		12      u32     ???
		16      u32     ???
		20      off     Palette
		24      off     Raster
		28      off     Sprites
		32      off     Anims[16]   // Paired with AnimNames
		96
	 * Palette contains a B5G5R5 palette. Raster contains concatenated
	 * sprite data, while Sprites contains each sprite dimensions and its
	 * offset within Raster.
	*/
	hdr = mp_slice(&desc->mp, 96);
	if (!hdr) {
		return wuerr(wu_unexpected_eof, "EOF on real sprite header");
	}

	const uint32_t pal_off = buf_endian32(hdr + 20, little_endian);
	desc->raster_off = buf_endian32(hdr + 24, little_endian);
	desc->sprite_off = buf_endian32(hdr + 28, little_endian);
	desc->anim_ptrs = hdr + 32;
	struct wu_st st = get_assemble_dims(desc);
	if (!wu_isok(st)) {
		return st;
	}

	desc->pal = palette_new();
	if (!desc->pal) {
		return WUERR_HERE(wu_alloc_error);
	}
	return get_pal(&desc->mp, desc->pal, pal_off);
}

static struct wu_st sir0_init(struct mparser *mp) {
	/* SIR0 header:
		Offset  Type    Name
		0       u8      ID[4]            // "SIR0"
		4       off     ContentHeader
		8       off     PointerList
		12      u32     Zero
		16      -       Content[]
		-       -       PackedPointers[]
	*/
	const uint8_t *hdr = mp_slice(mp, 12);
	if (hdr) {
		const uint8_t id[4] = {'S', 'I', 'R', '0'};
		if (!memcmp(hdr, id, sizeof(id))) {
			uint32_t header = buf_endian32(hdr + 4, little_endian);
			mp_seek_set(mp, header);
			return wuok();
		}
		return WUERR_HERE(wu_unknown_file_type);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

struct wu_st sir0_spr_init(struct sir0_spr_desc *desc, const struct wuptr mem) {
	*desc = (struct sir0_spr_desc) {.mp = mp_wuptr(mem)};
	const struct wu_st st = sir0_init(&desc->mp);
	return wu_isok(st) ? sprite_parse(desc) : st;
}


/* Chunsoft AT6P, used in 999 for the Nintendo DS.
 * Format explanation:
https://github.com/PhoenixBound/at6p
 * Programming track: Ternary Game */

void at6p_cleanup(struct at6p_desc *desc) {
	free(desc->decomp);
}

struct wu_st at6p_load(struct at6p_desc *desc, struct wuimg *img) {
	const size_t size = wuimg_size(img);
	const struct wuptr data = mp_avail(&desc->mp, size);
	if (data.len >= size) {
		img->data = (uint8_t *)data.ptr;
		img->borrowed = true;
	} else if (wuimg_alloc_noverify(img)) {
		memcpy(img->data, data.ptr, data.len);
	} else {
		return WUERR_HERE(wu_alloc_error);
	}
	return wuok();
}

struct wu_st at6p_info(struct at6p_desc *desc, struct wuimg *img) {
	struct wu_st st = sir0_init(&desc->mp);
	if (!wu_isok(st)) {
		return st;
	}

	/* Image header:
		Offset  Type    Name
		0       u32     X1      [*]
		4       u32     Y1
		8       u32     X2
		12      u32     Y2
		16      u32     ???
		20      off     ???
		24      off     Raster
		28      off     Palette
		32      off     ???
		36
	 * [*] Raster bounds in 8x8 tile units.
	*/
	const uint8_t *hdr = mp_slice(&desc->mp, 36);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint32_t x1 = buf_endian32(hdr, little_endian);
	const uint32_t y1 = buf_endian32(hdr+4, little_endian);
	const uint32_t x2 = buf_endian32(hdr+8, little_endian);
	const uint32_t y2 = buf_endian32(hdr+12, little_endian);
	img->w = (x2 - x1 + 1)*8;
	img->h = (y2 - y1 + 1)*8;
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 5;
	struct palette *pal = wuimg_palette_init(img);
	if (!pal) {
		return WUERR_HERE(wu_alloc_error);
	}

	const uint32_t raster_off = buf_endian32(hdr + 24, little_endian);
	const uint32_t pal_off = buf_endian32(hdr + 28, little_endian);
	st = get_pal(&desc->mp, pal, pal_off);
	if (wu_isok(st)) {
		mp_seek_set(&desc->mp, raster_off);
		return wuerr(wuimg_verify(img), NULL);
	}
	return st;
}

static struct wu_st at6p_decomp(struct at6p_desc *desc) {
	/* AT6P header (after signature):
		Offset  Type    Name
		0       u8      Unused
		1       u16     CompSize
		3       u8      Unused[9]
		12      u24     DecompSize
		15      u8      Unused
		16      u8      InitVal
		17      u8      Zero
		18      u8      CompData[CompSize]
	*/
	const uint8_t *hdr = mp_slice(&desc->mp, 18);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint32_t dst_len = buf_endian24(hdr + 12, little_endian);
	if (!dst_len) {
		return wuerr(wu_no_image_data, "no at6p stream");
	}

	uint8_t *dst = malloc(dst_len);
	if (!dst) {
		return WUERR_HERE(wu_alloc_error);
	}

	struct bitstrm bs;
	bitstrm_from_wuptr(&bs, mp_remaining(&desc->mp));
	uint32_t d = 0;
	uint8_t val = hdr[16];
	uint8_t prev = hdr[17]; /* I don't think this is correct, but it'd be
		nice if it were */
	dst[0] = val;
	++d;
	while (d < dst_len) {
		uint32_t b = bitstrm_lsb_exp_golomb(&bs);
		switch (b) {
		case 0: // repeat current byte, don't update prev
			break;
		case 1: // swap current and prev
			;uint8_t tmp = prev;
			prev = val;
			val = tmp;
			break;
		default: // set prev to current byte, then update current
			;bool sign = b & 1;
			prev = val;
			val = (uint8_t)(val + (int)(b >> 1) * (sign ? -1 : 1));
		}
		dst[d] = val;
		++d;
	}
	desc->decomp = dst;
	desc->mp = mp_mem(d, dst);
	return wuerr_partial(d, dst_len);
}

struct wu_st at6p_unpack(struct at6p_desc *desc, const struct wuptr map) {
	*desc = (struct at6p_desc) {.mp = mp_wuptr(map)};
	const uint8_t sig[] = {'A', 'T', '6', 'P'};
	const enum wu_error st = fmt_sigcmp_mem(sig, sizeof(sig), &desc->mp);
	if (st == wu_ok) {
		return at6p_decomp(desc);
	}
	return WUERR_HERE(st);
}
