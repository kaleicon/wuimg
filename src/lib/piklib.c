// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <string.h>

#include "misc/endian.h"
#include "raster/strip.h"
#include "lib/piklib.h"

/* Aidem Media Piklib engine image */

static void unaligned_byte_swap(uint8_t *data, size_t len) {
	for (size_t i = 0; i < len; ++i) {
		const uint8_t tmp = data[i*2];
		data[i*2] = data[i*2+1];
		data[i*2+1] = tmp;
	}
}

struct wu_st piklib_load(struct piklib_desc *desc, struct wuimg *img) {
	struct sewing_machine sew;
	strip_sew_init(&sew, img->data, NULL, img->w, img->h, 2, img->align_sh,
		desc->mask_size);

	size_t read = fread(sew.color.ptr, 1, sew.color.len, desc->ifp);
	if (which_end() != little_endian) {
		unaligned_byte_swap(sew.color.ptr, read/2);
	}
	if (desc->mask_size) {
		if (read < sew.color.len) {
			return wuerr(wu_unexpected_eof,
				"stream truncated before mask could be read");
		} else if (!strip_sew_alloc_alpha(&sew)) {
			return wuerr(wu_alloc_error,
				"failed to allocate alpha mask buffer");
		}
		read += fread(sew.alpha.ptr, 1, sew.alpha.len, desc->ifp);
		strip_sew_alpha(&sew);
		strip_sew_free_alpha(&sew);
	}
	return wuerr_partial(read,
		sew.color.len + (desc->mask_size ? sew.alpha.len : 0));
}

struct wu_st piklib_init(struct piklib_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* PIKLIB header:
		Offset  Type    Name
		0       char    Magic[4]     // "PIK\0"
		4       u32     Width
		8       u32     Height
		12      u32     BitsUsed     // 16 (565) or 15 (555)
		16      u32     ColorSize
		20      u32     ???          // Always Zero?
		24      u32     Compression?
		28      u32     MaskSize
		32      u8      ???[8]
		40

	 * Afterwards comes BGR565 data and optional A8 mask if uncompressed
	 * If compressed:
		40      u32     UncompSize
		44      u32     CompSize     // ColorSize - 8
		48      u8      Stream[CompSize]
	*/

	const uint8_t magic[] = {'P', 'I', 'K', 0};
	uint32_t hdr[10];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	if (endian32l(hdr[6]) != 0) {
		return wuerr(wu_unsupported_feature,
			"compressed PIK files unsupported");
	}

	img->w = endian32l(hdr[1]);
	img->h = endian32l(hdr[2]);
	img->channels = 1;
	img->bitdepth = 16;
	img->layout = pix_bgra;

	desc->ifp = ifp;
	desc->mask_size = endian32l(hdr[7]);
	const uint32_t bits = endian32l(hdr[3]);
	switch (bits) {
	case 15: case 16: break;
	default: return wuerr(wu_invalid_header, "bitdepth != 15 or 16");
	}
	uint16_t id = bits == 15 ? 0x555 : 0x565;
	struct bitfield *bf;
	if (desc->mask_size) {
		// what in the world
		img->bitdepth = 24;
		bool l = which_end() == little_endian;
		if (l) {
			id |= 0x8000;
		} else {
			id = (uint16_t)(id << 8 | 0x8);
			img->layout = pix_abgr;
		}
		bf = wuimg_bitfield_from_id(img, id);
		if (bf && bits == 15) {
			bf->comp[l ? 3 : 0].shr += 1;
		}
	} else {
		bf = wuimg_bitfield_from_id(img, id);
	}
	return bf ? WU_OK : WUERR_HERE(wu_alloc_error);
}
