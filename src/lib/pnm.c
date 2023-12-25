// SPDX-License-Identifier: 0BSD
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

static void scale_32(const struct pnm_desc *desc, uint32_t *dst,
const size_t dims) {
	const unsigned maxval = desc->scale.pnm;
	if (maxval == UINT_MAX && !desc->sign) {
		endian_loop32(dst, desc->endian, dims);
	} else {
		const uint64_t mul = ((uint64_t)UINT_MAX << 32) / maxval + 1;
		const uint64_t add = (desc->sign) ? maxval/2 + 1 : 0;
		for (size_t i = 0; i < dims; ++i) {
			const uint64_t val = (endian32(dst[i], desc->endian) + add) * mul;
			dst[i] = (uint32_t)(val >> 32);
		}
	}
}

static void scale_16(const struct pnm_desc *desc, uint16_t *dst,
const size_t dims) {
	const unsigned maxval = desc->scale.pnm;
	if (maxval == USHRT_MAX && !desc->sign) {
		endian_loop16(dst, desc->endian, dims);
	} else {
		const uint32_t mul = ((uint32_t)USHRT_MAX << 16) / maxval + 1;
		const uint32_t add = (desc->sign) ? maxval/2 + 1 : 0;
		for (size_t i = 0; i < dims; ++i) {
			const uint32_t val = (endian16(dst[i], desc->endian) + add) * mul;
			dst[i] = (uint16_t)(val >> 16);
		}
	}
}

static void pfm_decode(const struct pnm_desc *desc, union int_real *out,
const size_t dims) {
	if (desc->scale.pfm == 1.0f) {
		endian_loop32(&out->bytes, desc->endian, dims);
	} else {
		for (size_t i = 0; i < dims; ++i) {
			out[i].real = endianf32(out[i].bytes, desc->endian)
				* desc->scale.pfm;
		}
	}
}

static size_t scale_raster(const struct pnm_desc *desc, void *restrict dst,
const size_t dims) {
	switch (desc->type) {
	case pnm_color_pfm:
	case pnm_gray_pfm:
		pfm_decode(desc, dst, dims);
		break;
	case pnm_color_phm:
	case pnm_gray_phm:
		// FIXME: Support f16 scaling.
		endian_loop16(dst, desc->endian, dims);
		break;
	default:
		switch (desc->bytedepth) {
		case 1:
			repack_scale(dst, dst, dims,
				repack_scale_info(desc->scale.pnm, 8),
				desc->sign ? pix_signed : pix_normal);
			break;
		case 2: scale_16(desc, dst, dims); break;
		case 4: scale_32(desc, dst, dims); break;
		}
		break;
	}
	return dims;
}

static size_t plain_ppm_decode(const struct pnm_desc *restrict desc,
void *restrict dst, const size_t dims) {
	size_t cnt = 0;
	size_t len = file_remaining(desc->ifp);
	uint8_t *src = malloc(len + 1);
	if (src) {
		len = file_tail(src, 1, len, desc->ifp);
		src[len] = 's'; // Sentinel
		struct mparser mp = mp_mem(len, src);

		const long range = (desc->scale.pnm > UCHAR_MAX)
			? USHRT_MAX : UCHAR_MAX;
		const long scale = (range << 16) / desc->scale.pnm + 1;
		const size_t digits = 5;
		mp_skip_space_unsafe(&mp);
		while (cnt < dims) {
			long val;
			if (!mp_scan_uint(&mp, digits, &val)
			|| val > desc->scale.pnm) {
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
	return cnt;
}

static size_t plain_pbm_decode(const struct pnm_desc *restrict desc,
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
					dst[cnt] = 0xff;
					++cnt;
					continue;
				case '1':
					dst[cnt] = 0x00;
					++cnt;
					continue;
				default: break;
				}
				break;
			}
		} while (cnt < dims);
		free(buf);
	}
	return cnt;
}

size_t pnm_decode(struct pnm_desc *desc, struct wuimg *img,
const size_t i) {
	if (!wuimg_clone(img, &desc->rast) || !wuimg_alloc_noverify(img)) {
		return 0;
	}
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
		return fread(dst, 1, size, desc->ifp);
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
	return scale_raster(desc, dst,
		fread(dst, desc->bytedepth, elems, desc->ifp));
}

/* Header parsing */

static size_t count_images(struct pnm_desc *desc) {
	const size_t total = file_remaining(desc->ifp);
	return zuceildiv(total, wuimg_size(&desc->rast));
}

static enum wu_error setup_desc(struct pnm_desc *desc) {
	if (!desc->rast.w || !desc->rast.h) {
		return wu_invalid_header;
	}

	bool is_half = false;
	switch (desc->type) {
	case pnm_raw_pbm:
		desc->rast.bitdepth = 1;
		desc->rast.attr = pix_inverted;
		break;
	case pnm_plain_pbm:
	case pnm_mtv:
		desc->scale.pnm = 0xff;
		desc->rast.bitdepth = 8;
		break;
	case pnm_xv_thumb:
		if (desc->scale.pnm != 255) {
			return wu_invalid_header;
		}
		desc->rast.bitdepth = 8;
		desc->rast.attr = pix_pack_332;
		break;
	case pnm_color_phm: case pnm_gray_phm:
		is_half = true;
		// fallthrough
	case pnm_color_pfm: case pnm_gray_pfm:
		if (!isnormal(desc->scale.pfm)) {
			return wu_invalid_header;
		}
		desc->rast.bitdepth = is_half ? 16 : 32;
		desc->rast.attr = pix_float;
		desc->rast.mirror = true;
		desc->endian = signbit(desc->scale.pfm)
			? little_endian : big_endian;
		desc->scale.pfm = fabsf(desc->scale.pfm);
		if (is_half && desc->scale.pfm != 1.0f) {
			return wu_unsupported_feature;
		}
		break;
	case pnm_plain_pgm: case pnm_plain_ppm:
	case pnm_raw_pgm: case pnm_raw_ppm:
	case pnm_pam:
		if (!desc->scale.pnm || desc->scale.pnm > USHRT_MAX) {
			return wu_invalid_header;
		}
		desc->rast.bitdepth = desc->scale.pnm > UCHAR_MAX ? 16 : 8;
	case pnm_pgx: break;
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
			return wu_invalid_header;
		}
	}

	const enum wu_error st = wuimg_verify(&desc->rast);
	if (st != wu_ok) {
		return st;
	}
	desc->bytedepth = desc->rast.bitdepth / 8;
	desc->data_start = ftell(desc->ifp);

	switch (desc->type) {
	case pnm_raw_pbm: case pnm_raw_pgm: case pnm_raw_ppm:
		desc->nr = count_images(desc);
		if (!desc->nr) {
			return wu_unexpected_eof;
		}
		break;
	default:
		desc->nr = 1;
		break;
	}
	return wu_ok;
}

static enum wu_error parse_pgx(struct pnm_desc *desc) {
	/* PGX data may have any bitdepth between 1 and 32, and are stored
	 * in the smallest word unit that can fit them. So 5-bit data is stored
	 * in 8-bit bytes, 9-bit data in 16-bit words, and 24-bit data in
	 * 32-bit words. They are stored as normal numbers in the containing
	 * word, with signed numbers in two's complement.
	 *   5-bit unsigned: 0x00 to 0x1f (0 to 32)
	 *   5-bit signed:   0xf0 to 0x0f (-16 to 15)
	*/
	char order[2];
	char sign[4];
	unsigned depth;
	char spaces[2];
	char newline[3];
	const int match = fscanf(desc->ifp,
		"%2c" "%3[ +-]" "%u"
		"%c" "%zu" "%c" "%zu" "%2[\r\n]",
		order, sign, &depth,
		spaces, &desc->rast.w, spaces + 1, &desc->rast.h, newline);
	if (match == EOF) {
		return wu_unexpected_eof;
	} else if (match != 8 || !depth || depth > 32
	|| memchk(spaces, ' ', sizeof(spaces))
	|| (strcmp(newline, "\n") && strcmp(newline, "\r\n")) ) {
		return wu_invalid_header;
	}

	if (!memcmp(order, "ML", sizeof(order))) {
		desc->endian = big_endian;
	} else if (!memcmp(order, "LM", sizeof(order))) {
		desc->endian = little_endian;
	} else {
		return wu_invalid_header;
	}

	if (sign[0] == ' ') {
		switch (sign[1]) {
		case 0: break;
		case ' ':
			if (sign[2] != 0) {
				return wu_invalid_header;
			}
			break;
		case '+': case '-':
			switch (sign[2]) {
			case 0: case ' ': break;
			default: return wu_invalid_header;
			}
			break;
		default: return wu_invalid_header;
		}
	} else {
		return wu_invalid_header;
	}

	desc->sign = (sign[1] == '-');
	desc->scale.pnm = bit_set32(depth);
	desc->rast.bitdepth = (unsigned char)bit_min_wordsize_bits(depth);
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

static enum wu_error match_pam_token(struct pnm_desc *desc, const char *token,
bool *finished) {
	unsigned long maxval;
	enum wu_error st;
	if (token[0] == '#' || !strcmp("TUPLTYPE", token)) {
		return skip_line(desc->ifp);
	} else if (!strcmp("WIDTH", token) || !strcmp("HEIGHT", token)) {
		maxval = SIZE_MAX;
	} else if (!strcmp("DEPTH", token)) {
		maxval = UCHAR_MAX;
	} else if (!strcmp("MAXVAL", token)) {
		maxval = USHRT_MAX;
	} else if (!strcmp("ENDHDR", token)) {
		*finished = true;
		return next_is_newline(desc->ifp);
	} else {
		return wu_invalid_header;
	}
	unsigned long val;
	st = scan_ul(desc->ifp, &val, maxval);
	if (st == wu_ok) {
		switch (token[0]) {
		case 'W': desc->rast.w = (size_t)val; break;
		case 'H': desc->rast.h = (size_t)val; break;
		case 'D': desc->rast.channels = (uint8_t)val; break;
		case 'M': desc->scale.pnm = (unsigned)val; break;
		}
		return next_is_newline(desc->ifp);
	}
	return st;
}

static enum wu_error parse_arbitrary_map(struct pnm_desc *desc) {
	bool finished = false;
	do {
		char token[10];
		switch (fscanf(desc->ifp, "%9s", token)) {
		case 1:
			;const enum wu_error st = match_pam_token(desc, token,
				&finished);
			if (st != wu_ok) {
				return st;
			}
			break;
		case EOF:
			return wu_unexpected_eof;
		default:
			return wu_invalid_header;
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
		}
	}
}

static enum wu_error parse_any_map(struct pnm_desc *desc) {
	int fields;
	switch (desc->type) {
	case pnm_plain_pbm:
	case pnm_raw_pbm:
	case pnm_mtv:
		fields = 2;
		break;
	default:
		fields = 3;
		break;
	}

	bool read_float = false;
	switch (desc->type) {
	case pnm_color_pfm: case pnm_gray_pfm:
	case pnm_color_phm: case pnm_gray_phm:
		read_float = true;
		break;
	default: break;
	}

	for (int seen = 0; seen < fields; ++seen) {
		bool space = false;
		const enum wu_error status = skip_any_junk(desc, &space);
		if (status != wu_ok) {
			return status;
		} else if (seen > 0 && !space) {
			return wu_invalid_header;
		}

		unsigned long val;
		enum wu_error st;
		bool parse_float = read_float && seen == 2;
		if (seen == 2) {
			if (parse_float) {
				st = scan_f(desc->ifp, &desc->scale.pfm);
			} else {
				st = scan_ul(desc->ifp, &val, USHRT_MAX);
			}
		} else {
			st = scan_ul(desc->ifp, &val, SIZE_MAX);
		}
		if (st != wu_ok) {
			return st;
		} else if (!parse_float) {
			switch (seen) {
			case 0: desc->rast.w = (size_t)val; break;
			case 1: desc->rast.h = (size_t)val; break;
			case 2: desc->scale.pnm = (unsigned)val; break;
			}
		}

	}

	const int c = getc(desc->ifp);
	if (c == EOF) {
		return wu_unexpected_eof;
	} else if (isspace(c)) {
		return setup_desc(desc);
	}
	return wu_invalid_header;
}

enum wu_error pnm_parse_header(struct pnm_desc *desc) {
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

static enum wu_error disambiguate(struct pnm_desc *desc,
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

enum wu_error pnm_open_file(struct pnm_desc *desc, FILE *ifp,
const bool maybe_mtv) {
	*desc = (struct pnm_desc) {
		.ifp = ifp,
		.endian = big_endian,
	};

	unsigned char magic[3];
	if (fread(magic, sizeof(magic), 1, ifp)) {
		if (magic[0] == 'P') {
			if (magic[1] == '7') {
				const enum wu_error st = disambiguate(desc,
					magic[2]);
				if (st != wu_ok) {
					return st;
				}
			} else {
				if (!isspace(magic[2])
				|| (magic[1] == pnm_pgx && magic[2] != ' ')) {
					return wu_invalid_signature;
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
				return wu_ok;
			case pnm_mtv: // Invalid here
				break;
			}
		} else if (maybe_mtv) {
			fseek(desc->ifp, -(long)sizeof(magic), SEEK_CUR);
			desc->type = pnm_mtv;
			return wu_ok;
		}
		return wu_invalid_signature;
	}
	return wu_unexpected_eof;
}
