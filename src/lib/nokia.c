// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "misc/endian.h"
#include "raster/fmt.h"
#include "nokia.h"
static void txt2bin(void *restrict data, const size_t len,
void *restrict _n) {
	(void)_n;
	uint8_t *dst = data;
	for (size_t i = 0; i < len; ++i) {
		dst[i] &= 1;
	}
}

struct wu_st nol_load(struct nol_desc *desc, struct wuimg *img) {
	if (wuimg_alloc_noverify(img)) {
		return wuerr_partial(
			fmt_load_raster_callback(img, desc->ifp, txt2bin, NULL),
			wuimg_size(img));
	}
	return WUERR_HERE(wu_alloc_error);
}

struct wu_st nol_parse(struct nol_desc *desc, struct wuimg *img, FILE *ifp) {
	/* NOL/NGG common header:
		Offset  Type    Name
		0       u8      Magic[4] # "NGG\0", "NOL\0"
		4       u16     Version? # 1
		6
	 * For NOL:
		6       u16     CountryCode
		8       u16     NetworkCode
		10
	 * Afterwards for both:
		+0      u16     Width
		+2      u16     Height
		+4      u16     Unknown1 # 1
		+6      u16     Unknown2 # 1
		+8      u16     Unknown3 # ???
		+10
	*/
	uint16_t hdr[10];
	if (!fread(hdr, 16, 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (endian16(hdr[2], little_endian) != 1) {
		return WUERR_HERE(wu_invalid_header);
	}

	*desc = (struct nol_desc) {
		.ifp = ifp,
	};
	const uint8_t nol[] = {'N', 'O', 'L', 0};
	const uint8_t ngg[] = {'N', 'G', 'G', 0};
	size_t i = 3;
	if (!memcmp(hdr, nol, sizeof(nol))) {
		if (!fread(hdr + 8, 4, 1, ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		desc->is_nol = true;
		desc->country = endian16(hdr[i], little_endian);
		desc->network = endian16(hdr[i+1], little_endian);
		i += 2;
	} else if (!memcmp(hdr, ngg, sizeof(ngg))) {
		// nothing
	} else {
		return WUERR_HERE(wu_invalid_header);
	}

	desc->mystery = endian16(hdr[i+4], little_endian);
	if (endian16(hdr[i+2], little_endian) != 1
	|| endian16(hdr[i+3], little_endian) != 1) {
		return wuerr(wu_uncertain_validity, "mystery fields != 1");
	}

	img->w = endian16(hdr[i], little_endian);
	img->h = endian16(hdr[i+1], little_endian);
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 1;
	img->attr = pix_inverted;
	return wuimg_verify_st(img);
}
