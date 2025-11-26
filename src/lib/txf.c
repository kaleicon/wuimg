// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <string.h>

#include "misc/bit.h"
#include "raster/fmt.h"
#include "lib/txf.h"

/* Spec from
https://web.archive.org/web/20010802003744/http://reality.sgi.com/mjk_asd/tips/TexFont/texfont.c
*/

enum txf_format {
	txf_format_byte = 0,
	txf_format_bit = 1,
};

struct wu_st txf_load(const struct txf_desc *desc, struct wuimg *img) {
	return fmt_load_raster_st(img, desc->ifp);
}

struct wu_st txf_parse(struct txf_desc *desc, struct wuimg *img, FILE *ifp) {
	/* TXF header (after signature):
		Offset  Type    Name
		0       u8      Magic[4]
		4       u32     Endian
		8       u32     Format            // 0 == 8-bit data, 1 == 1-bit
		12      u32     Width
		16      u32     Height
		20      u32     Ascent
		24      u32     Descent
		28      u32     Glyphs
		32      struct  GlyphInfo[Glyphs]
		...     u8      Texture[]         // Stored bottom-up

	 * GlyphInfo struct:
		0       u16     Chara
		2       u8      Width
		3       u8      Height
		4       s8      XOff
		5       s8      YOff
		6       s8      Advance
		7       u8      Padding
		8       u16     X
		10      u16     Y
		12

	 * For 1-bit data, lower bits are leftmost in the image.
	 * The interpretation of Chara seems application dependent.
	*/

	const uint8_t magic[4] = {0xff, 't', 'x', 'f'};
	uint32_t hdr[8];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	enum endianness e;
	const uint8_t big[4] = {0x12, 0x34, 0x56, 0x78};
	const uint8_t little[4] = {0x78, 0x56, 0x34, 0x12};
	if (!memcmp(hdr + 1, little, sizeof(little))) {
		e = little_endian;
	} else if (!memcmp(hdr + 1, big, sizeof(big))) {
		e = big_endian;
	} else {
		return wuerr(wu_invalid_header, "unrecognized endianness");
	}

	switch (endian32(hdr[2], e)) {
	case txf_format_byte: img->bitdepth = 8; break;
	case txf_format_bit: img->bitdepth = 1; break;
	default: return wuerr(wu_invalid_header, "unrecognized raster depth");
	}
	img->w = endian32(hdr[3], e);
	img->h = endian32(hdr[4], e);
	img->channels = 1;
	img->bit = img->bitdepth == 1 ? little_endian : big_endian;
	img->mirror = true;

	*desc = (struct txf_desc) {
		.ifp = ifp,
		.max_ascent = endian32(hdr[5], e),
		.max_descent = endian32(hdr[6], e),
	};
	const uint32_t glyphs = endian32(hdr[7], e);
	fseek(ifp, 12 * glyphs, SEEK_CUR);
	return WU_OK;
}
