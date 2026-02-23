// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <ctype.h>
#include <string.h>

#include "misc/common.h"
#include "misc/math.h"
#include "lib/ea.h"

/* Electronic Arts Fonts (FNTF, FNTI, FNTS)
 * FNTF and FNTS are essentially the same format. Basic info:
https://web.archive.org/web/20231013181321/https://forum.xentax.com/viewtopic.php?f=33&t=22120
https://web.archive.org/web/20230518105443/https://wiki.xentax.com/index.php/EA_SFN_Font

 * FNTI is somewhat different but describes the texture in the same way.
*/

static struct wu_st parse_eafnt(struct eafnt_desc *desc, struct wuimg *img) {
	/* FFN & SFN char table:
		Offset  Type    Name
		0       u16     Character
		2       u16     ???
		4       u16     X
		6       u16     Y
		8       u16     Width
		10      u16     Height
		12
	 * Table: [1]
		0       u32     Len
		4       u32     Content[Len * ?] // [2]
	 * buy high sell low:
		?       char    hodl[8]          // "Buy ERTS"
		?+8     u8      Zeros[]
	 * Image:
		0       u8      RasterTypeOrBitflags?
		1       u8      MoreBitflags?
		2       u16     HeightRelatedSomething?
		4       u16     Width
		6       u16     Height
		8       u8      Padding?[8]
		16      u8      Raster
	 * [1] Supposedly a Palette, but contents are definitively
	 *     not colors, and length never matches the image depth
	 *     in the samples I have.
	 * [2] Multiplier varies. May be an enumerated type.
	*/
	// TODO: Display character table in metadata?

	uint8_t hdr[16];
	fseek(desc->ifp, (long)desc->img_off, SEEK_SET);
	if (!fread(hdr, sizeof(hdr), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	img->w = buf_endian16l(hdr+4);
	img->h = buf_endian16l(hdr+6);
	img->channels = 1;
	desc->image_code = hdr[0];
	switch (desc->image_code) {
	case 0x7d: // A8_R8_G8_B8, or maybe A8_B8_G8_R8?
		img->bitdepth = 8;
		img->channels = 4;
		img->layout = pix_rgba; // as big endian
		break;
	case 0x7b: // R8
	case 0x64:
		img->bitdepth = 8;
		break;
	case 0x7a: // R4
		img->bitdepth = 4;
		break;
	case 0x79:
	case 0x01:
		/* TODO: Not sure what's going on, but makes all samples
		 * display ok...ish */
		;const uint16_t what = buf_endian16l(hdr + 2);
		if (what) {
			// Most files are like this
			img->bitdepth = 4;
			img->bit = little_endian;
		} else {
			// Only way to get SSERIF8.FFN to display
			img->bitdepth = 1;
		}
		break;
	case 0x6d: // A4_R4_G4_B4
		img->bitdepth = 4;
		img->channels = 4;
		img->layout = pix_layout_pack(3, 0, 1, 2); // as big endian
		break;
	default:
		return wuerr(wu_uncertain_validity, "weird image type code");
	}
	return WU_OK;
}

struct wu_st eafnt_init(struct eafnt_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* FFN & SFN header:
		Offset  Type    Name
		0       char    ID[4]       // F[Nn][Tt][FS]
		4       u32     Filesize?   // Sometimes less
		8       u16     Unknown
		10      u16     Characters
		12      u8      ???[8]
		20      u32     CharTableOffset
		24      u32     UnknownOffset
		28      u32     ImageOffset
		32

	 * FNTI header:
		Offset  Type    Name
		0       char    ID[4]       // F[Nn][Tt]I
		4       s16?    ???         // A signed number, maybe
		6       u16?    ???[3]      // always less than Filesize?
		12      u32     Filesize
		16      u16     AlwaysOne?
		18      u16     Offset1
		20      u16     Offset2
		22      u16     Offset3
		24      u16     Offset4
		26      u16     Offset5
		28      u32     ImageOffset // [*]
		32      struct  CharXYPos?  // Just eyeballing
		Offset1

	 * [*] The difference between the Offset fields seems to be always
	 *     (Offset1 - 0x20)/4. Maybe this is character data split into
	 *     multiple tables?
	*/

	uint8_t hdr[32];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	hdr[1] = (uint8_t)toupper(hdr[1]);
	hdr[2] = (uint8_t)toupper(hdr[2]);
	const uint8_t fnt[3] = {'F', 'N', 'T'};
	if (!memcmp(hdr, fnt, sizeof(fnt))) {
		const uint32_t img_off = buf_endian32l(hdr + 28);
		*desc = (struct eafnt_desc) {
			.ifp = ifp,
			.img_off = img_off,
		};
		switch (hdr[3]) {
		case 'F':
		case 'S':
			desc->chars = buf_endian16l(hdr + 10);
			desc->char_off = buf_endian32l(hdr + 20);
			desc->unk_off = buf_endian32l(hdr + 24);
			return parse_eafnt(desc, img);
		case 'I':
			;uint16_t chars = buf_endian16l(hdr + 18);
			desc->chars = chars > 0x20 ? (uint16_t)(chars - 0x20)/4 : 0;
			desc->char_off = 0x20;
			return parse_eafnt(desc, img);
		}
	}
	return WUERR_HERE(wu_invalid_header);
}
