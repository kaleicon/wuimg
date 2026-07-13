// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "raster/fmt.h"
#include "lib/dvm.h"

/* From the Encyclopedia of Graphic File Formats.
 * 6 years and we're still referencing it. What a book.
*/

void dvm_cleanup(struct dvm_desc *desc) {
	wustr_free(&desc->text);
}

struct wu_st dvm_load_frame(struct dvm_desc *desc, struct wuimg *img,
const size_t i) {
	fseek(desc->ifp, (long)(desc->off + desc->frame_size*i), SEEK_SET);
	if (desc->pal == dvm_pal_per_frame) {
		if (!palette_from_file(img->u.palette, 3, 1 << desc->depth,
		desc->ifp, img->bitrange)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
	}
	return fmt_load_raster_st(img, desc->ifp);
}

static uint8_t u8round(uint8_t i, unsigned mul, unsigned scale) {
	return (uint8_t)((i * mul + scale/2) / 0x100);
}

static void generate_palette(struct pix_rgba8 *pix) {
	const uint8_t max = 0x3f;
	const unsigned scale = 1 << 8;
	for (uint8_t i = 0; i < 16; ++i) {
		// round(i * 4.2) == floor(i * 63/15 + 0.5)
		const uint8_t val = u8round(i, 0x3f * scale / 15, scale);
		pix[i] = (struct pix_rgba8) {
			.r = val, .g = val, .b = val, .a = max,
		};
	}
	for (uint8_t x = 0; x < 6; ++x) {
		// round(x * 12.6) == floor(i * 63/5 + 0.5)
		const unsigned mul = 0x3f * scale / 5;
		const uint8_t r = u8round(x, mul, scale);
		for (uint8_t y = 0; y < 6; ++y) {
			const uint8_t g = u8round(y, mul, scale);
			for (uint8_t z = 0; z < 6; ++z) {
				pix[x*6*6 + y*6 + z + 16] = (struct pix_rgba8) {
					.r = r, .g = g, .b = u8round(z, mul, scale),
					.a = max,
				};
			}
		}
	}
	for (uint8_t i = 0; i < 8; ++i) {
		pix[232+i] = (struct pix_rgba8) {
			.r = i*9, .a = max,
		};
		pix[240+i] = (struct pix_rgba8) {
			.g = i*9, .a = max,
		};
		pix[248+i] = (struct pix_rgba8) {
			.b = i*9, .a = max,
		};
	}
}

struct wu_st dvm_parse(struct dvm_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* DVM header:
		Offset  Type    Name
		0       char    ID[3]
		3       char    Type    # 'F' or 'Q' for Version 1, 'V'
	 * If Type == 'V':
		4       u8      Version # Major << 4 | Minor
		5       u8      Flags
		*       0 1 2 3 4 5 6 7
		*       | | | | | | | +-0: 160x120 (Quarter screen)
		*       | | | | | | |   1: 320x240 (Full screen)
		*       U | | | | | +---0: Unpacked raster. 1: Packed.
		*       N | | | | +-----0: Standard palette. 1: Embedded palette.
		*       U | | +-\-------0: No text. 1: Has text.
		*       S | +----+------With (Bit 2 << 1) | Bit 4:
		*       E |             00: 4-bit raster
		*       D |             01: 8-bit
		*       | |             10: 2-bit
		*       : |             11: 1-bit
		*       . +-------------0: Per frames palettes
		*                       1: Embedded palette is global
	 * For all types:
		+0      u16     Millisecs
	 * If there's Text:
		+1      u16     TextLen
		+2      u8      Text[TextLen]
	 * If there's a global embedded palette:
		+0      u8      RGB[1 << Depth][3]
	 * Afterwards comes frame data.
	*/
	const uint8_t id[] = {'D', 'V', 'M'};
	uint8_t hdr[10];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, id, sizeof(id))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	*desc = (struct dvm_desc) {
		.ifp = ifp,
		.depth = 4,
		.pal = dvm_pal_per_frame,
		.off = 4,
	};
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 6;
	bool full = false;
	bool text = false;
	switch (hdr[3]) {
	case 'F': full = true; break;
	case 'Q': break;
	case 'V':
		desc->version = hdr[desc->off];
		const uint8_t f = hdr[desc->off+1];
		desc->off += 2;
		text = f & 0x08;
		full = f & 0x80;
		const bool embedded_pal = f & 0x20;
		const bool global_pal = f & 0x02;
		desc->pal = embedded_pal + (global_pal & embedded_pal);
		switch ((f & 0x10) >> 4 | (f & 0x04) >> 1) {
		case 0: desc->depth = 4; break;
		case 1: desc->depth = 8; break;
		case 2: desc->depth = 2; break;
		case 3: desc->depth = 1; break;
		}
		img->bitdepth = (f & 0x40) ? desc->depth : 8;
		break;
	default:
		return WUERR_HERE(wu_invalid_header);
	}
	img->w = 160u << full;
	img->h = 100u << full;
	const uint16_t msec = buf_endian16(hdr + desc->off, little_endian);
	desc->off += 2;
	if (text) {
		const uint16_t text_len = buf_endian16(hdr + desc->off, little_endian);
		desc->off += 2;
		if (!wustr_malloc(&desc->text, text_len)) {
			return WUERR_HERE(wu_alloc_error);
		} else if (!fread(desc->text.str, text_len, 1, ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		desc->off += text_len;
	} else {
		fseek(ifp, (long)desc->off, SEEK_SET);
	}

	desc->frame_size = img->w / (8/img->bitdepth) * img->h;
	struct palette *pal = wuimg_palette_init(img);
	if (!pal) {
		return WUERR_HERE(wu_alloc_error);
	}
	switch (desc->pal) {
	case dvm_pal_standard:
		generate_palette(pal->color);
		break;
	case dvm_pal_per_frame:
		desc->frame_size += 3u << desc->depth;
		break;
	case dvm_pal_global:
		if (!palette_from_file(pal, 3, 1u << desc->depth, ifp,
		img->bitrange)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		desc->off += 3u << desc->depth;
		break;
	}

	fseek(ifp, 0, SEEK_END);
	const size_t nr = ((size_t)ftell(ifp) - desc->off) / desc->frame_size;
	if (!wuimg_anim_init(img, nr)) {
		return WUERR_HERE(wu_alloc_error);
	}
	for (size_t i = 0; i < nr; ++i) {
		wuimg_anim_frame_set(img, i, msec, 1000, true);
	}
	return WU_OK;
}
