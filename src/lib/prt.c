// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
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

size_t prt_decode(const struct prt_desc *desc, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}

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
			return 0;
		}

		w += fread(sew.alpha.ptr, 1, sew.alpha.len, desc->ifp);
		turn_mask(&sew);
		strip_sew_free_alpha(&sew);
	}
	return w;
}

static enum wu_error validate_header(struct prt_desc *desc, struct wuimg *img,
const uint16_t bitdepth, const uint16_t width, const uint16_t height,
const bool mask) {
	switch (bitdepth) {
	case 8: case 24: case 32: break;
	default: return wu_invalid_header;
	}
	desc->depth = (uint8_t)bitdepth;
	desc->mask = mask;

	img->w = width;
	img->h = height;
	img->channels = mask ? 4 : (uint8_t)(desc->depth / 8);
	img->bitdepth = 8;
	wuimg_align(img, 4);
	img->layout = pix_bgra;
	img->alpha = (img->channels == 1) ? alpha_ignore : alpha_unassociated;
	img->mirror = true;
	return wu_ok;
}

enum wu_error prt_parse(struct prt_desc *desc, struct wuimg *img) {
	/* PRT header (after magic bytes):
		Offset  Size    Name
		0       u16     Version       // 101 or 102
		2       u16     Bitdepth
		4       u16     PaletteOffset
		6       u16     DataOffset
		8       u16     Width
		10      u16     Height
		12	u32     HasMask
		16

	 * Version 102 fields:
		Offset  Size    Name
		16      u32     XOffset
		20      u32     YOffset
		24      u32     Width // Width again? I'm as confused as you
		28      u32     Height
		32

	 * Notes:
	 *  · A bitdepth of 8 always uses a palette.
	 *  · The raster is stored bottom-up and has an alignment of 4.
	 *  · The mask is top-down and has an alignment of 1.
	*/
	uint8_t buf[16];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	enum wu_error st = validate_header(desc, img,
		buf_endian16(buf + 2, little_endian),
		buf_endian16(buf + 8, little_endian),
		buf_endian16(buf + 10, little_endian),
		buf_endian32(buf + 12, little_endian));
	if (st != wu_ok) {
		return st;
	}

	const uint16_t pal_offset = buf_endian16(buf + 4, little_endian);
	const uint16_t data_offset =  buf_endian16(buf + 6, little_endian);
	const enum prt_version version = buf_endian16(buf, little_endian);
	switch (version) {
	case prt_v102:
		if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
			return wu_unexpected_eof;
		}
		desc->x = buf_endian32(buf, little_endian);
		desc->y = buf_endian32(buf + 4, little_endian);
		// fallthrough
	case prt_v101:
		desc->version = version;
		break;
	default: return wu_invalid_header;
	}

	if (desc->depth == 8) {
		fseek(desc->ifp, pal_offset, SEEK_SET);
		struct palette *pal = palette_new();
		if (!pal) {
			return wu_alloc_error;
		}

		st = fmt_load_pal(desc->ifp, pal, fmt_pal_rgbx, 256);
		if (st != wu_ok) {
			return st;
		}
		if (desc->mask) {
			desc->pal = pal;
		} else {
			wuimg_palette_set(img, pal);
		}
	}
	fseek(desc->ifp, data_offset, SEEK_SET);
	return wuimg_verify(img);
}

enum wu_error prt_open(struct prt_desc *desc, FILE *ifp) {
	const uint8_t magic[4] = "PRT\0";
	*desc = (struct prt_desc) {
		.ifp = ifp,
	};
	return fmt_sigcmp(magic, sizeof(magic), ifp);
}
