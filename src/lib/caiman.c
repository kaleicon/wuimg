// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <string.h>

#include "lib/caiman.h"

// Caiman GFX Data File

struct wu_st caiman_img_info(struct caiman_desc *desc, struct wuimg *img,
const uint16_t idx) {
	uint8_t off_data[36];
	fseek(desc->ifp, desc->off + idx*(long)sizeof(off_data), SEEK_SET);
	if (!fread(off_data, sizeof(off_data), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	fseek(desc->ifp, (long)buf_endian32l(off_data + 32) + desc->off - 17,
		SEEK_SET);

	/* GFX BMP header:
		Offset  Type    Name
		0       u8      InfoID[4]
		4       u32     HeaderSize  # after this field
		8       u16     Width
		10      u16     Height
		12      u8      PixelDepth
		13      u16     Flags?
		15      u16     XCenter?
		17      u16     YCenter?
		19      s16     ???[2]      # -1 if Flags & 4
		23      u8      ???[32]     # always 0 (reserved space?)
		55      u8      DataID[4]
		59

	 * Flag bits:
		& 1     0 = BGR, 1 = RGB
		& 2     always set?
		& 3     0 = opaque texture, 1 = magenta is transparent?
	*/
	const uint8_t info_hdr[8] = {'i','n','f','o',0x2f,0,0,0};
	uint8_t hdr[0x2f + sizeof(info_hdr)];
	if (!fread(hdr, sizeof(hdr), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, info_hdr, sizeof(info_hdr))) {
		return wuerr(wu_invalid_header, "bad info header");
	}
	img->w = buf_endian16l(hdr + 8);
	img->h = buf_endian16l(hdr + 10);
	img->bitdepth = 8;
	img->layout = hdr[13] & 1 ? pix_rgba : pix_bgra;

	struct wutree *meta = wuimg_get_metadata(img);
	if (meta) {
		off_data[32] = 0;
		tree_add_leaf(meta, "Name", (char *)off_data, NULL);
		meta = tree_add_branch(meta, "Center");
		if (meta) {
			tree_bud_leaf_u(meta, "X", buf_endian16l(hdr + 15));
			tree_bud_leaf_u(meta, "Y", buf_endian16l(hdr + 17));
		}
	}

	switch (hdr[12]) {
	case 8:
		img->channels = 1;
		img->alpha = alpha_ignore;
		const uint8_t pal_hdr[4] = {'p','a','l',' '};
		if (!fread(hdr, sizeof(pal_hdr), 1, desc->ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		} else if (memcmp(hdr, pal_hdr, sizeof(pal_hdr))) {
			return wuerr(wu_invalid_header,
				"expected palette header");
		}
		struct wu_st st = wuimg_palette_from_file(img, 4, 256,
			desc->ifp);
		if (!wu_isok(st)) {
			return st;
		}
		break;
	case 24:
		img->channels = 3;
		break;
	default:
		return wuerr(wu_invalid_header, "pixel depth is not 8 nor 24");
	}
	const uint8_t data_hdr[4] = {'d','a','t','a'};
	if (!fread(hdr, sizeof(data_hdr), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, data_hdr, sizeof(data_hdr))) {
		return wuerr(wu_invalid_header, "expected raster data header");
	}

	return WU_OK;
}

struct wu_st caiman_init(struct caiman_desc *desc, FILE *ifp) {
	/* GFX header:
		Offset  Type    Name
		0       u8      Magic[15]
		15      u16     NrImages
		17      struct  FileOffset

	 * FileOffset struct:
		0       u8      Name[32]
		32      u32     Offset
		36
	*/
	const uint8_t magic[15] = {
		'G','F','X',' ',
		'D','a','t','a',' ',
		'F','i','l','e','.',
		0x1a,
	};
	uint8_t hdr[17];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	*desc = (struct caiman_desc) {
		.ifp = ifp,
		.off = ftell(ifp),
		.nr_images = buf_endian16l(hdr + sizeof(magic)),
	};
	return WU_OK;
}
