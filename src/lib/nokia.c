// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "misc/endian.h"
#include "raster/fmt.h"
#include "nokia.h"

/* Nokia Logo Manager */

const char * nlm_logo_type_str(const enum nlm_logo_type logo) {
	switch (logo) {
	case nlm_operator: return "Operator";
	case nlm_caller: return "Caller";
	case nlm_startup: return "Startup";
	case nlm_picture: return "Picture";
	}
	return "???";
}

struct wu_st nlm_load(const struct nlm_desc *desc, struct wuimg *img,
const uint8_t i) {
	if (wuimg_alloc_noverify(img)) {
		fseek(desc->ifp, 10 + (long)wuimg_size(img)*i, SEEK_SET);
		return fmt_load_raster_st(img, desc->ifp);
	}
	return WUERR_HERE(wu_alloc_error);
}

struct wu_st nlm_image_info(const struct nlm_desc *desc, struct wuimg *img) {
	img->w = desc->w;
	img->h = desc->h;
	img->channels = 1;
	img->bitdepth = 1;
	img->attr = pix_inverted;
	return wuimg_verify_st(img);
}

struct wu_st nlm_parse(struct nlm_desc *desc, FILE *ifp) {
	/* Nokia Logo Manager header:
		Offset  Type    Name
		0       u8      Magic[5] # "NLM \x01"
		5       u8      LogoType # 0 to 3
		6       u8      NrImages # Bias of -1
		7       u8      Width
		8       u8      Height
		9       u8      One
		10
	 * This is followed by NrImages+1 1-bit rasters.
	*/
	const uint8_t magic[5] = {'N', 'L', 'M', ' ', 1};
	uint8_t buf[10];
	if (!fread(buf, sizeof(buf), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(buf, magic, sizeof(magic)) || buf[5] > 3) {
		return WUERR_HERE(wu_invalid_header);
	} else if (buf[9] != 1) {
		return wuerr(wu_uncertain_validity, "buf[9] != 1");
	}
	*desc = (struct nlm_desc) {
		.ifp = ifp,
		.logo_type = buf[5],
		.nr_images = buf[6] + 1,
		.w = buf[7],
		.h = buf[8],
	};
	return wuok();
}


/* Nokia Operator Logo and Nokia Group Graphics */

static void txt2bin(void *restrict data, const size_t len,
void *restrict _n) {
	(void)_n;
	uint8_t *dst = data;
	for (size_t i = 0; i < len; ++i) {
		dst[i] &= 1;
	}
}

struct wu_st nol_load(const struct nol_desc *desc, struct wuimg *img) {
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
	 * This is followed by a monochrome raster encoded as ASCII '0' and '1'
	*/
	uint16_t buf[10];
	if (!fread(buf, 16, 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (endian16(buf[2], little_endian) != 1) {
		return WUERR_HERE(wu_invalid_header);
	}

	*desc = (struct nol_desc) {
		.ifp = ifp,
	};
	const uint8_t nol[] = {'N', 'O', 'L', 0};
	const uint8_t ngg[] = {'N', 'G', 'G', 0};
	size_t i = 3;
	if (!memcmp(buf, nol, sizeof(nol))) {
		if (!fread(buf + 8, 4, 1, ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		desc->is_nol = true;
		desc->country = endian16(buf[i], little_endian);
		desc->network = endian16(buf[i+1], little_endian);
		i += 2;
	} else if (!memcmp(buf, ngg, sizeof(ngg))) {
		// nothing
	} else {
		return WUERR_HERE(wu_invalid_header);
	}

	desc->mystery = endian16(buf[i+4], little_endian);
	if (endian16(buf[i+2], little_endian) != 1
	|| endian16(buf[i+3], little_endian) != 1) {
		return wuerr(wu_uncertain_validity, "mystery fields != 1");
	}

	img->w = endian16(buf[i], little_endian);
	img->h = endian16(buf[i+1], little_endian);
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 1;
	img->attr = pix_inverted;
	return wuimg_verify_st(img);
}


/* Nokia Picture Message */

struct wuptr npm_get_comment(const struct npm_desc *desc) {
	return (struct wuptr) {.len = desc->len, .ptr = desc->comment};
}

struct wu_st npm_load(const struct npm_desc *desc, struct wuimg *img) {
	if (wuimg_alloc_noverify(img)) {
		return fmt_load_raster_st(img, desc->ifp);
	}
	return WUERR_HERE(wu_alloc_error);
}

struct wu_st npm_parse(struct npm_desc *desc, struct wuimg *img, FILE *ifp) {
	/* Nokia Picture Message header:
		Offset  Type    Name
		0       u8      Magic[4] # "NPM\0"
		4       u8      Length
		5       char    Comment[Length]
		+0      u8      Zero
		+1      u8      Width
		+2      u8      Height
		+3      u8      One
		+4      u8      One
		+5      u8      Unknown
		+6
	 * This is followed by a 1-bit raster. */
	const uint8_t magic[4] = {'N', 'P', 'M', 0};
	uint8_t buf[6];
	if (!fread(buf, 5, 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(buf, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_header);
	}

	desc->ifp = ifp;
	desc->len = buf[4];
	if (!fread(desc->comment, desc->len, 1, ifp)
	|| !fread(buf, sizeof(buf), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (buf[0] || buf[3] != 1 || buf[4] != 1) {
		return wuerr(wu_uncertain_validity, "bytes[0,3,4] != {0,1,1}");
	}
	desc->mystery = buf[5];
	img->w = buf[1];
	img->h = buf[2];
	img->channels = 1;
	img->bitdepth = 1;
	img->attr = pix_inverted;
	return wuimg_verify_st(img);
}
