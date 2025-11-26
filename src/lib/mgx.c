// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "misc/mem.h"
#include "raster/fmt.h"
#include "lib/mgx.h"

/* Micrografx Icon
 * Not sure if single channel files are paletted or gray scale.
*/

struct wu_st mgxicn_load(struct mgxicn_desc *desc, struct wuimg *img) {
	++desc->idx;
	return fmt_load_raster_st(img, desc->ifp);
}

struct wu_st mgxicn_set_next(struct mgxicn_desc *desc, struct wuimg *img) {
	/* Micrografx Image header:
		Offset  Type    Name
		0       u8      SubImages?     // 0 (one image) or 1 (two)
		1       char    ImageType[20?] // [*]
		21      char    Name?[20?]     // [*]
		41
	 * [*] Null terminated. Trailing bytes could be garbage
	*/
	uint8_t buf[0x29];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (buf[0] > 1) {
		return wuerr(wu_uncertain_validity, "first image header byte > 1");
	}
	img->w = 80;
	img->h = 60 * (size_t)(buf[0] + 1);
	img->channels = desc->color == mgxicn_color_rgba8 ? 4 : 1;
	img->bitdepth = 8;
	img->mirror = 1;
	img->alpha = alpha_ignore;
	struct wutree *metadata = wuimg_get_metadata(img);
	if (metadata) {
		tree_add_leaf_len(metadata, "Type",
			wuptr_mem(buf + 1, strnlen((char *)buf + 1, 20)), NULL);
		tree_add_leaf_len(metadata, "Name",
			wuptr_mem(buf + 21, strnlen((char *)buf + 21, 20)), NULL);
	}
	return WU_OK;
}

struct wu_st mgxicn_init(struct mgxicn_desc *desc, FILE *ifp) {
	/* Micrografx Icon header:
		Offset  Type    Name
		0       char    ID[4]     // "ZZZZ"
		4       u32     Version?  // 0x164
		8       u8      ColorType // 0 = RGBA8, 1 = Gray8?
		9       u8      Zeros?[27]
		36      u32     NrImages
		40      struct  Image[NrImages]
	*/
	uint32_t hdr[10];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memchk(hdr, 'Z', sizeof(*hdr))) {
		return WUERR_HERE(wu_invalid_header);
	}
	if (endian32(hdr[1], little_endian) == 0x164
	&& !memchk(hdr + 3, 0, sizeof(*hdr)*6)) {
		const uint32_t color = endian32(hdr[2], little_endian);
		switch (color) {
		case mgxicn_color_rgba8:
		case mgxicn_color_gray8:
			*desc = (struct mgxicn_desc) {
				.ifp = ifp,
				.color = color,
				.nr = endian32(hdr[9], little_endian),
			};
			return WU_OK;
		}
		return wuerr(wu_uncertain_validity, "color type > 1");
	}
	return wuerr(wu_uncertain_validity, "unexpected header values");
}
