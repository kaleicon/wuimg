// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "misc/endian.h"
#include "raster/fmt.h"
#include "nokia.h"

/* Various undocumented Nokia formats, with help from
https://cgit.git.savannah.gnu.org/cgit/gnokii.git/tree/common/gsm-filetypes.c
*/

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
	fseek(desc->ifp, 10 + (long)wuimg_size(img)*i, SEEK_SET);
	return fmt_load_raster_st(img, desc->ifp);
}

struct wu_st nlm_image_info(const struct nlm_desc *desc, struct wuimg *img) {
	img->w = desc->w;
	img->h = desc->h;
	img->channels = 1;
	img->bitdepth = 1;
	img->cs.invert = true;
	return WU_OK;
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
	return WU_OK;
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
	return wuerr_partial(
		fmt_load_raster_callback(img, desc->ifp, txt2bin, NULL),
		wuimg_size(img));
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
	} else if (endian16l(buf[2]) != 1) {
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
		desc->country = endian16l(buf[i]);
		desc->network = endian16l(buf[i+1]);
		i += 2;
	} else if (!memcmp(buf, ngg, sizeof(ngg))) {
		// nothing
	} else {
		return WUERR_HERE(wu_invalid_header);
	}

	desc->mystery = endian16l(buf[i+4]);
	if (endian16l(buf[i+2]) != 1 || endian16l(buf[i+3]) != 1) {
		return wuerr(wu_uncertain_validity, "mystery fields != 1");
	}

	img->w = endian16l(buf[i]);
	img->h = endian16l(buf[i+1]);
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 1;
	img->cs.invert = true;
	return WU_OK;
}


/* Nokia Picture Message */

struct wuptr npm_get_comment(const struct npm_desc *desc) {
	return (struct wuptr) {.len = desc->len, .ptr = desc->comment};
}

struct wu_st npm_load(const struct npm_desc *desc, struct wuimg *img) {
	return fmt_load_raster_st(img, desc->ifp);
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
	img->cs.invert = true;
	return WU_OK;
}


/* Nokia Startup Logo */
void nsl_clean(struct nsl_desc *desc) {
	wustr_free(&desc->vers);
	wustr_free(&desc->modl);
}

#define NSL_BUF_SIZE (84*48/8)

struct wu_st nsl_load(struct nsl_desc *desc, struct wuimg *img) {
	uint8_t buf[NSL_BUF_SIZE];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	/* Raster is laid out in 8-row bands, each byte containing a column. */
	for (size_t ty = 0; ty < img->h/8; ++ty) {
		for (size_t x = 0; x < img->w; ++x) {
			uint8_t c = buf[ty * img->w + x];
			for (size_t y = 0; y < 8; ++y) {
				img->data[(ty*8+y)*img->w + x] = (
					(c >> y) & 1
				);
			}
		}
	}
	return WU_OK;
}

static struct wu_st load_str(struct wustr *str, FILE *ifp, uint16_t len) {
	if (!str->str) {
		if (wustr_malloc(str, len)) {
			return fread(str->str, len, 1, ifp)
				? WU_OK : WUERR_HERE(wu_unexpected_eof);
		}
		return WUERR_HERE(wu_alloc_error);
	}
	return wuerr(wu_uncertain_validity, "duplicate chunks");
}

struct wu_st nsl_parse(struct nsl_desc *desc, struct wuimg *img, FILE *ifp) {
	/* NSL is an IFF-like format, except lengths are stored as 16-bits. */
	const uint32_t form = FOURCC('F', 'O', 'R', 'M');
	const uint32_t vers = FOURCC('V', 'E', 'R', 'S');
	const uint32_t modl = FOURCC('M', 'O', 'D', 'L');
	const uint32_t nsld = FOURCC('N', 'S', 'L', 'D');
	const uint32_t comm = FOURCC('C', 'O', 'M', 'M');
	uint16_t buf[3];
	*desc = (struct nsl_desc){.ifp = ifp};
	for (bool first = true; fread(buf, sizeof(buf), 1, ifp); first = false) {
		const uint32_t cc = buf_endian32(buf, big_endian);
		const uint16_t len = endian16(buf[2], big_endian);
		if (first) {
			if (cc != form) {
				return wuerr(wu_invalid_header,
					"first chunk is not FORM");
			}
		} else if (cc == vers || cc == modl) {
			const struct wu_st st = load_str(
				cc == vers ? &desc->vers : &desc->modl,
				ifp, len);
			if (!wu_isok(st)) {
				return st;
			}
		} else if (cc == comm) {
			fseek(ifp, len, SEEK_CUR);
		} else if (cc == nsld) {
			if (len != 0x1f8) {
				return wuerr(wu_uncertain_validity,
					"NSLD chunk len != 0x1f8");
			}
			img->w = 84;
			img->h = 48;
			img->channels = 1;
			img->bitdepth = 8;
			img->bitrange = 1;
			img->cs.invert = true;
			return WU_OK;
		} else {
			return wuerr(wu_uncertain_validity,
				"unexpected chunk");
		}
	}
	return WUERR_HERE(wu_unexpected_eof);
}
