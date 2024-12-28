// SPDX-License-Identifier: 0BSD
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

static void load_callback(void *restrict data, size_t len,
void *restrict user) {
	(void)user;
	uint8_t *row = data;
	for (size_t i = 0; i < len; ++i) {
		row[i] = bit_rev8(row[i]);
	}
}

size_t txf_load(const struct txf_desc *desc, struct wuimg *img) {
	size_t read = 0;
	if (wuimg_alloc_noverify(img)) {
		read = (img->bitdepth == 1)
			? fmt_load_raster_callback(img, desc->ifp, load_callback, NULL)
			: fmt_load_raster(img, desc->ifp);
	}
	return read;
}

enum wu_error txf_parse(struct txf_desc *desc, struct wuimg *img) {
	/* TXF header (after signature):
		Offset  Type    Name
		0       u32     Endian
		4       u32     Format            // 0 == 8-bit data, 1 == 1-bit
		8       u32     Width
		12      u32     Height
		16      u32     Ascent
		20      u32     Descent
		24      u32     Glyphs
		28      struct  GlyphInfo[Glyphs]
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

	uint32_t hdr[7];
	if (!fread(hdr, sizeof(hdr), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	enum endianness e;
	const uint8_t big[4] = {0x12, 0x34, 0x56, 0x78};
	const uint8_t little[4] = {0x78, 0x56, 0x34, 0x12};
	if (!memcmp(hdr, little, sizeof(little))) {
		e = little_endian;
	} else if (!memcmp(hdr, big, sizeof(big))) {
		e = big_endian;
	} else {
		return wu_invalid_header;
	}

	switch (endian32(hdr[1], e)) {
	case txf_format_byte: img->bitdepth = 8; break;
	case txf_format_bit: img->bitdepth = 1; break;
	default: return wu_invalid_header;
	}
	img->w = endian32(hdr[2], e);
	img->h = endian32(hdr[3], e);
	img->channels = 1;
	img->mirror = true;

	desc->max_ascent = endian32(hdr[4], e);
	desc->max_descent = endian32(hdr[5], e);
	const uint32_t glyphs = endian32(hdr[6], e);
	fseek(desc->ifp, 12 * glyphs, SEEK_CUR);
	return wuimg_verify(img);
}

enum wu_error txf_init(struct txf_desc *desc, FILE *ifp) {
	*desc = (struct txf_desc) {.ifp = ifp};
	const uint8_t sig[4] = "\xfftxf";
	return fmt_sigcmp(sig, sizeof(sig), ifp);
}
