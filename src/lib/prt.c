// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <string.h>

#include "raster/fmt.h"
#include "raster/strip.h"

#include "prt.h"

void prt_cleanup(struct prt_desc *desc) {
	palette_unref(desc->pal);
}

static void turn_mask(struct sewing_machine *sew) {
	for (size_t y = 0; y < sew->h; ++y) {
		uint8_t *dst_row = sew->dst.ptr + sew->dst.stride*y;
		uint8_t *color_row = sew->color.ptr + sew->color.stride*y;
		uint8_t *alpha_row = sew->alpha.ptr
			+ sew->alpha.stride * (sew->h - y - 1);
		strip_handsew_alpha(dst_row, color_row, alpha_row, sew->w,
			sew->pal, sew->ch);
	}
}

struct wu_st prt_decode(const struct prt_desc *desc, struct wuimg *img) {
	const uint8_t ch = (uint8_t)(desc->depth / 8);
	struct sewing_machine sew;
	strip_sew_init(&sew, img->data, desc->pal, img->w, img->h, ch,
		img->align_sh, desc->mask);
	size_t w = fread(sew.color.ptr, 1, sew.color.len, desc->ifp);
	if (w && desc->mask) {
		/* Modify alpha to have an alignment of 1 */
		sew.alpha.stride = img->w;
		sew.alpha.len = img->w * img->h;
		if (!strip_sew_alloc_alpha(&sew)) {
			return WUERR_HERE(wu_alloc_error);
		}

		w += fread(sew.alpha.ptr, 1, sew.alpha.len, desc->ifp);
		turn_mask(&sew);
		strip_sew_free_alpha(&sew);
	}
	return wuerr_partial(w, sew.color.len + (desc->mask ? sew.alpha.len : 0));
}

struct wu_st prt_parse(struct prt_desc *desc, struct wuimg *img, FILE *ifp) {
	/* PRT header:
		Offset  Type    Name
		0       u8      Magic[4]
		4       u16     Version       // 101 or 102
		6       u16     Bitdepth
		8       u16     PaletteOffset
		10      u16     DataOffset
		12      u16     Width
		14      u16     Height
		16	u32     HasMask
		20

	 * Version 102 fields:
		Offset  Size    Name
		20      u32     XOffset
		24      u32     YOffset
		28      u32     ???
		32      u32     ???
		36

	 * Notes:
	 *  · A bitdepth of 8 always uses a palette.
	 *  · Raster is stored bottom-up and has an alignment of 4.
	 *  · Mask is top-down and has an alignment of 1.
	*/
	const uint8_t magic[4] = {'P', 'R', 'T', 0};
	uint8_t buf[20];
	if (!fread(buf, sizeof(buf), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(buf, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	const enum prt_version version = buf_endian16l(buf + 4);
	const uint16_t depth = buf_endian16l(buf + 6);
	const uint16_t pal_offset = buf_endian16l(buf + 8);
	const uint16_t data_offset =  buf_endian16l(buf + 10);
	img->w = buf_endian16l(buf + 12);
	img->h = buf_endian16l(buf + 14);
	*desc = (struct prt_desc) {
		.ifp = ifp,
		.mask = buf_endian32l(buf + 16),
		.version = version,
		.depth = (uint8_t)depth,
	};
	switch (version) {
	case prt_v102:
		if (!fread(buf, 16, 1, ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		desc->x = buf_endian32l(buf);
		desc->y = buf_endian32l(buf + 4);
		break;
	case prt_v101:
		break;
	default: return wuerr(wu_invalid_header, "unknown version");
	}

	switch (depth) {
	case 8:
		fseek(ifp, pal_offset, SEEK_SET);
		struct palette *pal = palette_new();
		if (!pal) {
			return WUERR_HERE(wu_alloc_error);
		} else if (!palette_from_file(pal, 4, 256, ifp, 8)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		if (desc->mask) {
			desc->pal = pal;
		} else {
			wuimg_palette_set(img, pal);
		}
		break;
	case 24: case 32: break;
	default: return wuerr(wu_invalid_header, "bitdepth != {8, 24, 32}");
	}
	fseek(ifp, data_offset, SEEK_SET);

	img->channels = desc->mask ? 4 : desc->depth/8;
	img->bitdepth = 8;
	wuimg_align(img, 4);
	img->layout = pix_bgra;
	img->alpha = (img->channels == 1) ? alpha_ignore : alpha_unassociated;
	img->mirror = true;
	return WU_OK;
}
