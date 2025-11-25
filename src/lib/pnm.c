// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <math.h>

#include "misc/bit.h"
#include "misc/file.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "misc/mparser.h"
#include "raster/fmt.h"
#include "raster/unpack.h"
#include "lib/pnm.h"

const char * pnm_type_str(const enum pnm_type type) {
	switch (type) {
	case pnm_plain_pbm: return "Text PBM";
	case pnm_plain_pgm: return "Text PGM";
	case pnm_plain_ppm: return "Text PPM";
	case pnm_raw_pbm: return "Raw PBM";
	case pnm_raw_pgm: return "Raw PGM";
	case pnm_raw_ppm: return "Raw PPM";
	case pnm_pam: return "PAM";
	case pnm_xv_thumb: return "Xv thumb";
	case pnm_color_pfm: return "Color PFM";
	case pnm_gray_pfm: return "Gray PFM";
	case pnm_color_phm: return "Color PHM";
	case pnm_gray_phm: return "Gray PHM";
	case pnm_mtv: return "MTV";
	case pnm_pgx: return "PGX";
	}
	return "???";
}

static struct wu_st scale_raster(const struct pnm_desc *desc, void *restrict dst,
const size_t read, const size_t dims) {
	switch (desc->type) {
	case pnm_color_pfm:
	case pnm_gray_pfm:
		if (desc->scale.pfm != 1.0f) {
			float *out = dst;
			for (size_t i = 0; i < read; ++i) {
				out[i] *= desc->scale.pfm;
			}
		}
		break;
	case pnm_color_phm:
	case pnm_gray_phm:
		// FIXME: Support f16 scaling.
		break;
	default:
		if (!desc->skip_scaling) {
			const uint8_t depth = desc->bytedepth * 8;
			remap_scale(dst, dst, read,
				remap_scale_info(desc->scale.pnm, depth,
					desc->rast.attr));
		}
	}
	return wuerr_partial(read, dims);
}

static struct wu_st plain_ppm_decode(const struct pnm_desc *restrict desc,
void *restrict dst, const size_t dims) {
	size_t cnt = 0;
	size_t len = file_remaining(desc->ifp);
	uint8_t *src = malloc(len + 1);
	if (src) {
		len = file_tail(src, 1, len, desc->ifp);
		src[len] = 's'; // Sentinel
		struct mparser mp = mp_mem(len, src);

		const uint32_t range = (desc->scale.pnm > UCHAR_MAX)
			? USHRT_MAX : UCHAR_MAX;
		const uint32_t scale = (desc->skip_scaling)
			? (1 << 16)
			: (range << 16) / desc->scale.pnm + 1;
		const size_t digits = 6;
		mp_skip_space_unsafe(&mp);
		while (cnt < dims) {
			uintmax_t val;
			if (!mp_scan_uint(&mp, digits, &val)) {
				break;
			}

			val = (val * scale) >> 16;
			if (desc->rast.bitdepth == 16) {
				unsigned short *out = dst;
				out[cnt] = (unsigned short)(val);
			} else {
				unsigned char *out = dst;
				out[cnt] = (unsigned char)(val);
			}
			++cnt;
			if (!mp_skip_space_unsafe(&mp)) {
				break;
			}
		}
		free(src);
	}
	return wuerr_partial(cnt, dims);
}

static struct wu_st plain_pbm_decode(const struct pnm_desc *restrict desc,
unsigned char *restrict dst, const size_t dims) {
	size_t cnt = 0;
	unsigned char *buf = malloc(BUFSIZ);
	if (buf) {
		do {
			const size_t read = fread(buf, 1, BUFSIZ, desc->ifp);
			if (!read) {
				break;
			}

			for (size_t i = 0; i < read && cnt < dims; ++i) {
				switch (buf[i]) {
				case '\t': case '\n': case '\v':
				case '\f': case '\r': case ' ':
					continue;
				case '0':
				case '1':
					dst[cnt] = !(buf[i] & 1);
					++cnt;
					continue;
				default: break;
				}
				break;
			}
		} while (cnt < dims);
		free(buf);
	}
	return wuerr_partial(cnt, dims);
}

struct wu_st pnm_get_raster(struct pnm_desc *desc, struct wuimg *img,
const size_t i) {
	const size_t size = wuimg_size(img);
	fseek(desc->ifp, desc->data_start + (long)(size * i), SEEK_SET);

	const size_t elems = img->w * img->h * img->channels;
	uint8_t *dst = img->data;
	switch (desc->type) {
	case pnm_plain_pbm:
		return plain_pbm_decode(desc, dst, elems);
	case pnm_plain_pgm:
	case pnm_plain_ppm:
		return plain_ppm_decode(desc, dst, elems);
	case pnm_xv_thumb:
	case pnm_raw_pbm:
	case pnm_mtv:
		return fmt_load_raster_st(img, desc->ifp);
	case pnm_raw_pgm:
	case pnm_raw_ppm:
	case pnm_pam:
	case pnm_color_pfm:
	case pnm_gray_pfm:
	case pnm_color_phm:
	case pnm_gray_phm:
	case pnm_pgx:
		break;
	}
	const size_t r = fmt_load_raster_swap(img, desc->ifp, desc->endian);
	return scale_raster(desc, dst, r / desc->bytedepth, elems);
}

struct wu_st pnm_get_info(struct pnm_desc *desc, struct wuimg *img) {
	if (!wuimg_clone(img, &desc->rast)) {
		// Shouldn't happen, but whatever
		return WUERR_HERE(wu_alloc_error);
	} else if (desc->type == pnm_xv_thumb
	&& !wuimg_bitfield_from_id(img, 0x332)) {
		return WUERR_HERE(wu_alloc_error);
	}
	return WU_OK;
}

/* Header parsing */
static size_t count_images(struct pnm_desc *desc) {
	const size_t total = file_remaining(desc->ifp);
	return zuceildiv(total, wuimg_size(&desc->rast));
}

static struct wu_st setup_desc(struct pnm_desc *desc) {
	bool is_half = false;
	switch (desc->type) {
	case pnm_raw_pbm:
		desc->rast.bitdepth = 1;
		desc->rast.cs.invert = true;
		break;
	case pnm_plain_pbm:
		desc->rast.bitdepth = 8;
		desc->rast.bitrange = 1;
		break;
	case pnm_mtv:
		desc->scale.pnm = 0xff;
		desc->rast.bitdepth = 8;
		break;
	case pnm_xv_thumb:
		if (desc->scale.pnm != 255) {
			return wuerr(wu_invalid_header,
				"xv: scale != 255");
		}
		desc->rast.bitdepth = 8;
		desc->rast.layout = pix_bgra;
		break;
	case pnm_color_phm: case pnm_gray_phm:
		is_half = true;
		// fallthrough
	case pnm_color_pfm: case pnm_gray_pfm:
		if (!isnormal(desc->scale.pfm)) {
			return wuerr(wu_invalid_header,
				"pfm/phm: scale is not a normal float");
		}
		desc->rast.bitdepth = is_half ? 16 : 32;
		desc->rast.attr = pix_float;
		desc->rast.mirror = true;
		desc->endian = signbit(desc->scale.pfm)
			? little_endian : big_endian;
		desc->scale.pfm = fabsf(desc->scale.pfm);
		if (is_half && desc->scale.pfm != 1.0f) {
			return wuerr(wu_unsupported_feature,
				"phm: half-floats with scale != 1");
		}
		break;
	case pnm_raw_pgm: case pnm_raw_ppm:
	case pnm_pam:
	case pnm_plain_pgm: case pnm_plain_ppm:
		if (!desc->scale.pnm || desc->scale.pnm > USHRT_MAX) {
			return wuerr(wu_invalid_header,
				"pgm/ppm/pam: scale < 1 || scale > 0xffff");
		}
		desc->rast.bitdepth = desc->scale.pnm > UCHAR_MAX ? 16 : 8;
		const uint32_t ones = bit_cto32(desc->scale.pnm);
		if (desc->scale.pnm >> ones == 0) {
			desc->rast.bitrange = (uint8_t)ones;
			desc->skip_scaling = true;
		}
		break;
	case pnm_pgx:
		desc->skip_scaling = true;
		break;
	}

	switch (desc->type) {
	case pnm_plain_pbm: case pnm_plain_pgm:
	case pnm_raw_pbm: case pnm_raw_pgm:
	case pnm_gray_pfm: case pnm_gray_phm:
	case pnm_xv_thumb: case pnm_pgx:
		desc->rast.channels = 1;
		break;
	case pnm_plain_ppm: case pnm_raw_ppm: case pnm_mtv:
	case pnm_color_pfm: case pnm_color_phm:
		desc->rast.channels = 3;
		break;
	case pnm_pam:
		if (!desc->rast.channels) {
			return wuerr(wu_invalid_header, "pam: no channels");
		}
	}

	const enum wu_error st = wuimg_verify(&desc->rast);
	if (st != wu_ok) {
		return WUERR_HERE(st);
	}
	desc->bytedepth = desc->rast.bitdepth / 8;
	desc->data_start = ftell(desc->ifp);

	switch (desc->type) {
	case pnm_raw_pbm: case pnm_raw_pgm: case pnm_raw_ppm:
		desc->nr = count_images(desc);
		if (!desc->nr) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		break;
	default:
		desc->nr = 1;
		break;
	}
	return WU_OK;
}

static struct wu_st parse_pgx(struct pnm_desc *desc) {
	/* PGX header as regex:
	 *	PG (ML|LM) [+-]? ?[0-9]+ [0-9]+ [0-9]+\r?\n
	 *	0   1       2      3      4      5     6
	 * 0: Signature (already read)
	 * 1: ML for big-endian, or LM for little-endian
	 * 2: Data is signed if '-', unsigned otherwise
	 * 3: Data depth
	 * 4: Width
	 * 5: Height
	 * 6: End of header, either \n or \r\n
	 * Fields are separated by exactly one space (0x20) character. And for
	 * whatever reason, there's an optional space in between the sign and
	 * the depth field.

	 * Data may have any bitdepth between 1 and 32, and pixels are stored
	 * in the smallest word unit that can fit them. So 5-bit data is stored
	 * in 8-bit bytes, 9-bit data in 16-bit words, and 24-bit data in
	 * 32-bit words. Only `depth` bits are used, and when data is signed,
	 * values are sign extended.
	 *   5-bit unsigned: 0x00 to 0x1f (0 to 32)
	 *   5-bit signed:   0xf0 to 0x0f (-16 to 15)
	*/

	char order[3];
	char sign[4];
	unsigned depth;
	char newline[3];
	const int match = fscanf(desc->ifp,
		"%2c"
		"%3[+ -]"
		"%3u" "%*1[ ]"
		"%zu" "%*1[ ]"
		"%zu" "%2[\r\n]",
		order, sign, &depth, &desc->rast.w, &desc->rast.h, newline);
	if (match == EOF) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (match != 6) {
		return wuerr(wu_invalid_header, "header fields missing");
	} else if (depth < 1 || depth > 32) {
		return wuerr(wu_invalid_header, "depth < 1 or depth > 32");
	}

	if (!strcmp(order, "ML")) {
		desc->endian = big_endian;
	} else if (!strcmp(order, "LM")) {
		desc->endian = little_endian;
	} else {
		return wuerr(wu_invalid_header, "bad endianness spec");
	}

	if (sign[0] != ' ') {
		return wuerr(wu_invalid_header, "expected space before sign");
	}
	switch (sign[1]) {
	case ' ':
		if (sign[2]) {
			return wuerr(wu_invalid_header,
				"too many spaces before depth field");
		}
		break;
	case '+': case '-':
		if (sign[2] != 0 && sign[2] != ' ') {
			return wuerr(wu_invalid_header, "repeated sign");
		}
		break;
	}

	switch (newline[0]) {
	case '\n':
		if (newline[1]) { // '\n' or '\r' that's part of raster
			ungetc(newline[1], desc->ifp);
		}
		break;
	case '\r':
		if (newline[1] != '\n') {
			return wuerr(wu_invalid_header,
				"bad end of header line");
		}
	}

	desc->scale.pnm = bit_set32(depth);
	desc->rast.bitdepth = bit_min_wordsize_bits(depth);
	desc->rast.attr = (sign[1] == '-') ? pix_signed : pix_normal;
	desc->rast.bitrange = (unsigned char)depth;
	return setup_desc(desc);
}

static enum wu_error scan_ul(FILE *ifp, unsigned long *ret,
const unsigned long maxval) {
	char buf[64];
	if (fscanf(ifp, "%63s", buf) > 0) {
		if (strchr(buf, '-')) {
			return wu_invalid_header;
		}
		char *endptr;
		*ret = strtoul(buf, &endptr, 10);
		if (*endptr == 0) {
			if (*ret <= maxval) {
				return wu_ok;
			}
			return wu_int_overflow;
		}
		return wu_invalid_header;
	}
	return wu_unexpected_eof;
}

static enum wu_error scan_f(FILE *ifp, float *ret) {
	char buf[64];
	if (fscanf(ifp, "%63s", buf) > 0) {
		if (strchr(buf, 'x') || strchr(buf, 'X')) {
			return wu_invalid_header;
		}
		char *endptr;
		*ret = strtof(buf, &endptr);
		if (*endptr == 0) {
			return wu_ok;
		}
		return wu_invalid_header;
	}
	return wu_unexpected_eof;
}

static enum wu_error next_is_newline(FILE *ifp) {
	switch (getc(ifp)) {
	case '\n': return wu_ok;
	case EOF: return wu_unexpected_eof;
	}
	return wu_invalid_header;
}

static enum wu_error skip_line(FILE *ifp) {
	enum wu_error st;
	do {
		st = next_is_newline(ifp);
	} while (st == wu_invalid_header);
	return st;
}

static struct wu_st match_pam_token(struct pnm_desc *desc, const char *token,
bool *finished) {
	unsigned long maxval;
	if (token[0] == '#' || !strcmp("TUPLTYPE", token)) {
		// Ignore comments and TUPLTYPE
		return WUERR_CHECK(skip_line(desc->ifp));
	} else if (!strcmp("WIDTH", token) || !strcmp("HEIGHT", token)) {
		maxval = SIZE_MAX;
	} else if (!strcmp("DEPTH", token)) {
		maxval = UCHAR_MAX;
	} else if (!strcmp("MAXVAL", token)) {
		maxval = USHRT_MAX;
	} else if (!strcmp("ENDHDR", token)) {
		*finished = true;
		return WUERR_CHECK(next_is_newline(desc->ifp));
	} else {
		return wuerr(wu_invalid_header, "unknown pam token");
	}
	unsigned long val;
	enum wu_error e = scan_ul(desc->ifp, &val, maxval);
	if (e == wu_ok) {
		switch (token[0]) {
		case 'W': desc->rast.w = (size_t)val; break;
		case 'H': desc->rast.h = (size_t)val; break;
		case 'D': desc->rast.channels = (uint8_t)val; break;
		case 'M': desc->scale.pnm = (unsigned)val; break;
		}
		return WUERR_CHECK(next_is_newline(desc->ifp));
	}
	return wuerr(e, "bad pam number");
}

static struct wu_st parse_arbitrary_map(struct pnm_desc *desc) {
	bool finished = false;
	do {
		char token[10];
		switch (fscanf(desc->ifp, "%9s", token)) {
		case 1:
			;const struct wu_st st = match_pam_token(desc, token,
				&finished);
			if (!wu_isok(st)) {
				return st;
			}
			break;
		case EOF:
			return WUERR_HERE(wu_unexpected_eof);
		default:
			return wuerr(wu_invalid_header,
				"unexpected whitespace before pam token");
		}
	} while (!finished);
	return setup_desc(desc);
}

static enum wu_error skip_any_junk(struct pnm_desc *desc, bool *space) {
	for (bool comment = false;;) {
		const int c = getc(desc->ifp);
		if (c == EOF) {
			return wu_unexpected_eof;
		} else if (!comment) {
			if (c == '#') {
				comment = true;
			} else if (isspace(c)) {
				*space = true;
				continue;
			} else if (isdigit(c) || c == '-' || c == '+') {
				ungetc(c, desc->ifp);
				return wu_ok;
			} else {
				return wu_invalid_header;
			}
		} else if (c == '\n') {
			comment = false;
			*space = true;
		}
	}
}

static struct wu_st parse_any_map(struct pnm_desc *desc) {
	int fields = 3;
	switch (desc->type) {
	case pnm_plain_pbm:
	case pnm_raw_pbm:
	case pnm_mtv:
		fields = 2;
		break;
	default: break;
	}

	bool read_float = false;
	switch (desc->type) {
	case pnm_color_pfm: case pnm_gray_pfm:
	case pnm_color_phm: case pnm_gray_phm:
		read_float = true;
		break;
	default: break;
	}

	/* This format would be a lot easier to parse if it didn't allow for
	 * for comments to appear anywhere. */
	for (int seen = 0; seen < fields; ++seen) {
		bool space = false;
		enum wu_error status = skip_any_junk(desc, &space);
		if (status != wu_ok) {
			return wuerr(status,
				"some non-comment junk between number fields");
		} else if (seen > 0 && !space) {
			return wuerr(wu_invalid_header,
				"no separating whitespace between fields");
		}

		unsigned long val = 0;
		bool parse_float = read_float && seen == 2;
		if (seen == 2) {
			if (parse_float) {
				status = scan_f(desc->ifp, &desc->scale.pfm);
			} else {
				status = scan_ul(desc->ifp, &val, USHRT_MAX);
			}
		} else {
			status = scan_ul(desc->ifp, &val, SIZE_MAX);
		}
		if (status != wu_ok) {
			return wuerr(status, "bad integer/float");
		} else if (!parse_float) {
			switch (seen) {
			case 0: desc->rast.w = (size_t)val; break;
			case 1: desc->rast.h = (size_t)val; break;
			case 2: desc->scale.pnm = (unsigned)val; break;
			}
		}

	}

	// After that, what follows should be image data
	switch(getc(desc->ifp)) {
	case '\n': return setup_desc(desc);
	case EOF: return WUERR_HERE(wu_unexpected_eof);
	default: break;
	}
	return wuerr(wu_invalid_header, "expected newline before raster data");
}

static struct wu_st pnm_parse_header(struct pnm_desc *desc) {
	switch (desc->type) {
	case pnm_pam:
		return parse_arbitrary_map(desc);
	case pnm_pgx:
		return parse_pgx(desc);
	default:
		break;
	}
	return parse_any_map(desc);
}

static enum wu_error disambiguate7(struct pnm_desc *desc,
const unsigned char next_char) {
	if (next_char == '\n') {
		desc->type = pnm_pam;
		return wu_ok;
	} else if (next_char == ' ') {
		desc->type = pnm_xv_thumb;
		const unsigned char more_magic[4] = "332\n";
		return fmt_sigcmp(more_magic, sizeof(more_magic), desc->ifp);
	}
	return wu_invalid_signature;
}

struct wu_st pnm_parse(struct pnm_desc *desc, FILE *ifp,
const bool maybe_mtv) {
	*desc = (struct pnm_desc) {
		.ifp = ifp,
		.endian = big_endian,
	};

	unsigned char magic[3];
	if (fread(magic, sizeof(magic), 1, ifp)) {
		if (magic[0] == 'P') {
			if (magic[1] == '7') {
				const enum wu_error e = disambiguate7(desc,
					magic[2]);
				if (e != wu_ok) {
					return WUERR_HERE(e);
				}
			} else {
				if (!isspace(magic[2])
				|| (magic[1] == pnm_pgx && magic[2] != ' ')) {
					return WUERR_HERE(wu_invalid_signature);
				}
				desc->type = (enum pnm_type)magic[1];
			}

			switch (desc->type) {
			case pnm_plain_pbm:
			case pnm_plain_pgm:
			case pnm_plain_ppm:
			case pnm_raw_pbm:
			case pnm_raw_pgm:
			case pnm_raw_ppm:
			case pnm_pam:
			case pnm_xv_thumb:
			case pnm_color_pfm:
			case pnm_gray_pfm:
			case pnm_color_phm:
			case pnm_gray_phm:
			case pnm_pgx:
				return pnm_parse_header(desc);
			case pnm_mtv: // Invalid here
				break;
			}
		} else if (maybe_mtv) {
			fseek(desc->ifp, -(long)sizeof(magic), SEEK_CUR);
			desc->type = pnm_mtv;
			return pnm_parse_header(desc);
		}
		return WUERR_HERE(wu_invalid_signature);
	}
	return WUERR_HERE(wu_unexpected_eof);
}
