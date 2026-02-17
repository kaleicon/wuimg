// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <string.h>

#include "lib/piff.h"

struct wu_st vvtp_next(FILE *ifp, struct wuimg *img) {
	/* BODY chunk:
		Offset  Type    Name
		0       u32     HeaderSize
		4       u32     ???
		8       u16     Width
		10      u16     Height
		12      u16     ???
		14      u16     Bitdepth
		16      u32     ARGBMask[4]
		32      u32     ???[8]
		64
	*/
	uint16_t hdr[20]; // read chunk and info header in one go
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	fseek(ifp, 32, SEEK_CUR); // skip unknown fields

	const uint8_t body[4] = {'B', 'O', 'D', 'Y'};
	if (memcmp(hdr, body, sizeof(body))) {
		return wuerr(wu_invalid_header, "got non-BODY chunk");
	} else if (buf_endian32l(hdr + 4) != 0x40) {
		return wuerr(wu_invalid_header, "BODY info size != 0x40");
	}

	img->w = endian16l(hdr[8]);
	img->h = endian16l(hdr[9]);
	img->channels = 1;
	const uint16_t depth = endian16l(hdr[11]);
	img->bitdepth = (uint8_t)depth;
	switch (depth) {
	case 4: case 8:
		img->alpha = alpha_ignore;
		img->bit = depth == 4 ? little_endian : big_endian;
		struct wu_st st = wuimg_palette_from_file(img, 4, 1u << depth,
			ifp);
		if (!wu_isok(st)) {
			return st;
		}
		break;
	case 16: case 24:
		img->layout = pix_bgra;
		const uint32_t mask[4] = {
			buf_endian32l(hdr + 18),
			buf_endian32l(hdr + 16),
			buf_endian32l(hdr + 14),
			buf_endian32l(hdr + 12),
		};
		enum wu_error e = wuimg_bitfield_from_mask(img, mask,
			mask[3] ? 4 : 3, img->bitdepth);
		if (e != wu_ok) {
			return WUERR_HERE(e);
		}
		break;
	default: return wuerr(wu_invalid_header, "bad bitdepth");
	}
	return WU_OK;
}

struct wu_st vvtp_init(FILE *ifp) {
	uint32_t hdr[3];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint8_t piff[4] = {'P', 'I', 'F', 'F'};
	const uint8_t vvtp[4] = {'V', 'V', 'T', 'P'};
	if (memcmp(hdr, piff, sizeof(piff))
	|| memcmp(hdr + 2, vvtp, sizeof(vvtp))) {
		return wuerr(wu_invalid_signature,
			"not a VVTP file");
	}
	return WU_OK;
}
