#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <math.h>

#include "common.h"
#include "raster/file.h"
#include "raster/memparser.h"
#include "lib/pnm.h"

union int_real {
	uint32_t bytes;
	float real;
};

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
	case pnm_mtv: return "MTV";
	case pnm_pgx: return "PGX";
	}
	return "???";
}

static void scale_32(uint32_t *dst, const size_t dims,
const unsigned int maxval, const unsigned int mask, const enum endianness endian) {
	if (maxval == UINT_MAX) {
		loop_endian32(dst, endian, dims);
	} else {
		const uint_fast64_t scale = ((uint64_t)UINT_MAX << 32)
			/ maxval + 1;
		for (size_t i = 0; i < dims; ++i) {
			uint_fast32_t val = endian32(dst[i], endian) & mask;
			dst[i] = (uint32_t)((val * scale) >> 32);
		}
	}
}

static void scale_16(uint16_t *dst, const size_t dims,
const unsigned int maxval, const unsigned int mask, const enum endianness endian) {
	if (maxval == USHRT_MAX) {
		loop_endian16(dst, endian, dims);
	} else {
		const uint_fast32_t scale = ((unsigned)USHRT_MAX << 16)
			/ maxval + 1;
		for (size_t i = 0; i < dims; ++i) {
			uint_fast32_t val = endian16(dst[i], endian) & mask;
			dst[i] = (uint16_t)((val * scale) >> 16);
		}
	}
}

static void scale_8(uint8_t *dst, const size_t dims,
const unsigned int maxval, const unsigned int mask) {
	if (maxval != UCHAR_MAX) {
		const uint_fast16_t scale = ((unsigned)UCHAR_MAX << 8) / maxval + 1;
		for (size_t i = 0; i < dims; ++i) {
			dst[i] = (uint8_t)(((dst[i] & mask) * scale) >> 8);
		}
	}
}

static size_t scale_raster(const struct pnm_desc *desc, void *restrict dst,
const size_t dims) {
	const unsigned int mask = (desc->type == pnm_pgx) ? desc->scale.pnm : ~0u;
	switch (desc->bytedepth) {
	case 1: scale_8(dst, dims, desc->scale.pnm, mask); break;
	case 2: scale_16(dst, dims, desc->scale.pnm, mask, desc->endian); break;
	case 4: scale_32(dst, dims, desc->scale.pnm, mask, desc->endian); break;
	}
	return dims;
}

static size_t pfm_decode(const struct pnm_desc *desc,
union int_real *out, const size_t dims) {
	const size_t read = fread(out, sizeof(*out), dims, desc->ifp);
	if (desc->scale.pfm == 1.0f) {
		loop_endian32(&out->bytes, desc->endian, read);
	} else {
		for (size_t i = 0; i < read; ++i) {
			const union int_real val =
				{.bytes = endian32(out[i].bytes, desc->endian)};
			out[i].real = val.real * desc->scale.pfm;
		}
	}
	return read;
}

static size_t plain_ppm_decode(const struct pnm_desc *restrict desc,
void *restrict dst, const size_t dims) {
	size_t cnt = 0;
	size_t len = (size_t)file_remaining(desc->ifp);
	uint8_t *src = malloc(len + 2);
	if (src) {
		src[0] = ' ';
		len = fread_tail(src + 1, 1, len, desc->ifp);
		src[len+1] = 'd'; // Sentinel
		struct mp_parser mp = mp_parser_mem(len, src);

		const long range = (desc->scale.pnm > UCHAR_MAX)
			? USHRT_MAX : UCHAR_MAX;
		const long scale = (range << 16) / desc->scale.pnm + 1;
		const size_t digits = 5;
		while (cnt < dims && mp_skip_space_unsafe(&mp)) {
			long val;
			if (!mp_get_uint(&mp, digits, &val)
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

size_t pnm_decode(const struct pnm_desc *desc, void *restrict dst,
const size_t i) {
	const size_t size = raster_size(&desc->rast);
	fseek(desc->ifp, desc->data_start + (long)(size * i), SEEK_SET);

	const size_t elems = desc->rast.w * desc->rast.h * desc->rast.ch;
	switch (desc->type) {
	case pnm_plain_pbm:
		return plain_pbm_decode(desc, dst, elems);
	case pnm_plain_pgm:
	case pnm_plain_ppm:
		return plain_ppm_decode(desc, dst, elems);
	case pnm_color_pfm:
	case pnm_gray_pfm:
		return pfm_decode(desc, dst, elems);
	case pnm_raw_pgm:
	case pnm_raw_ppm:
	case pnm_pam:
	case pnm_pgx:
	case pnm_raw_pbm:
	case pnm_xv_thumb:
	case pnm_mtv:
		break;
	}
	return scale_raster(desc, dst,
		fread(dst, desc->bytedepth, elems, desc->ifp));
}

/* Header parsing */

static size_t count_images(struct pnm_desc *desc) {
	const size_t len = file_size_from(desc->ifp, desc->data_start);
	return len ? zumax(1, len / raster_size(&desc->rast)) : 0;
}

static enum wu_error setup_desc(struct pnm_desc *desc) {
	if (!desc->rast.w || !desc->rast.h) {
		return wu_invalid_header;
	}

	switch (desc->type) {
	case pnm_raw_pbm:
		desc->rast.bitdepth = 1;
		desc->rast.attr = pix_inverted;
		break;
	case pnm_plain_pbm:
	case pnm_mtv:
		desc->rast.bitdepth = 8;
		break;
	case pnm_xv_thumb:
		if (desc->scale.pnm != 255) {
			return wu_invalid_header;
		}
		desc->rast.bitdepth = 8;
		desc->rast.attr = pix_packing_332;
		break;
	case pnm_color_pfm: case pnm_gray_pfm:
		if (fpclassify(desc->scale.pfm) != FP_NORMAL) {
			return wu_invalid_header;
		}
		desc->rast.bitdepth = 32;
		desc->rast.attr = pix_float;
		desc->rast.mirror = true;
		desc->endian = signbit(desc->scale.pfm)
			? little_endian : big_endian;
		desc->scale.pfm = fabsf(desc->scale.pfm);
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
	case pnm_gray_pfm: case pnm_xv_thumb: case pnm_pgx:
		desc->rast.ch = 1;
		break;
	case pnm_plain_ppm: case pnm_raw_ppm: case pnm_mtv: case pnm_color_pfm:
		desc->rast.ch = 3;
		break;
	case pnm_pam:
		if (!desc->rast.ch) {
			return wu_invalid_header;
		}
	}

	if (!raster_normalize(&desc->rast)) {
		return wu_int_overflow;
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
	char order[2];
	char sign;
	int match = fscanf(desc->ifp, "%2c%*1[ ]%c", order, &sign);
	if (match != 2) {
		return wu_invalid_header;
	}
	if (!memcmp(order, "ML", ARRAY_LEN(order))) {
		desc->endian = big_endian;
	} else if (!memcmp(order, "LM", ARRAY_LEN(order))) {
		desc->endian = little_endian;
	} else {
		return wu_invalid_header;
	}

	int c = sign;
	if (sign == '+' || sign == '-') {
		c = getc(desc->ifp);
		switch (c) {
		case EOF: return wu_unexpected_eof;
		case ' ': break;
		default: ungetc(c, desc->ifp);
		}
	} else {
		if (isdigit(c)) {
			ungetc(c, desc->ifp);
		} else if (c != ' ') {
			return wu_invalid_header;
		}
	}

	char newline[3];
	unsigned depth;
	match = fscanf(desc->ifp, "%u%*1[ ]%zu%*1[ ]%zu%2[\r\n]",
		&depth, &desc->rast.w, &desc->rast.h, newline);
	if (match != 4 || (strcmp(newline, "\n") && strcmp(newline, "\r\n")) ) {
		return wu_invalid_header;
	}

	desc->scale.pnm = ~0u >> (32 - depth);
	depth = (depth - 1) / 8 + 1;
	switch (depth) {
	case 1: case 2: case 4: break;
	default: return wu_invalid_header;
	}
	desc->rast.bitdepth = (unsigned char)(depth * 8);
	desc->bytedepth = (unsigned char)depth;
	if (sign == '-') {
		desc->rast.attr = pix_signed;
	}
	return setup_desc(desc);
}

static enum wu_error skip_line(FILE *ifp) {
	for (;;) {
		switch (getc(ifp)) {
		case '\n': return wu_ok;
		case EOF: return wu_unexpected_eof;
		}
	}
	return wu_unexpected_eof;
}

static enum wu_error read_pam_token(FILE *ifp, const char *fmt,
void *where, const bool cur_val) {
	if (!cur_val) {
		unsigned char newline;
		switch (fscanf(ifp, fmt, where, &newline)) {
		case 2:
			if (newline == '\n') {
				return wu_ok;
			}
			break;
		case EOF:
			return wu_unexpected_eof;
		}
	}
	return wu_invalid_header;
}

static enum wu_error match_pam_token(struct pnm_desc *desc, const char *token,
bool *finished) {
	if (token[0] == '#') {
		return skip_line(desc->ifp);
	} else if (!strcmp("WIDTH", token)) {
		return read_pam_token(desc->ifp, " %zu%c", &desc->rast.w, desc->rast.w);
	} else if (!strcmp("HEIGHT", token)) {
		return read_pam_token(desc->ifp, " %zu%c", &desc->rast.h, desc->rast.h);
	} else if (!strcmp("DEPTH", token)) {
		return read_pam_token(desc->ifp, " %hhu%c", &desc->rast.ch, desc->rast.ch);
	} else if (!strcmp("MAXVAL", token)) {
		return read_pam_token(desc->ifp, " %hu%c", &desc->scale.pnm,
			desc->scale.pnm);
	} else if (!strcmp("TUPLTYPE", token)) {
		return skip_line(desc->ifp);
	} else if (!strcmp("ENDHDR", token)) {
		switch (getc(desc->ifp)) {
		case EOF: return wu_unexpected_eof;
		case '\n':
			*finished = true;
			return wu_ok;
		}
	}
	return wu_invalid_header;
}

static enum wu_error parse_arbitrary_map(struct pnm_desc *desc) {
	char token[9];
	bool finished = false;
	enum wu_error status;
	while (!finished) {
		switch(fscanf(desc->ifp, "%8s", token)) {
		case 1:
			status = match_pam_token(desc, token, &finished);
			if (status != wu_ok) {
				return status;
			}
			break;
		case EOF:
			return wu_unexpected_eof;
		default:
			return wu_invalid_header;
		}
	}
	return setup_desc(desc);
}

static enum wu_error skip_any_junk(struct pnm_desc *desc) {
	for (bool comment = false;;) {
		const int c = getc(desc->ifp);
		if (!comment) {
			if (c == '#') {
				comment = true;
			} else if (isspace(c)) {
				continue;
			} else if (isdigit(c) || c == '-') {
				ungetc(c, desc->ifp);
				return wu_ok;
			} else {
				return wu_invalid_header;
			}
		} else if (c == '\n') {
			comment = false;
		} else if (c == EOF) {
			return wu_unexpected_eof;
		}
	}
}

static enum wu_error parse_any_map(struct pnm_desc *desc) {
	// Comments may appear at any point
	int seen = 0;
	enum wu_error status;
	while ((status = skip_any_junk(desc)) == wu_ok) {
		int result;
		if (seen == 0) {
			result = fscanf(desc->ifp, "%zu", &desc->rast.w);
		} else if (seen == 1) {
			result = fscanf(desc->ifp, "%zu", &desc->rast.h);
		} else {
			if (desc->type == pnm_color_pfm || desc->type == pnm_gray_pfm) {
				result = fscanf(desc->ifp, "%f", &desc->scale.pfm);
			} else {
				result = fscanf(desc->ifp, "%u", &desc->scale.pnm);
			}
		}

		if (result == 0) {
			status = wu_invalid_header;
			break;
		} else if (result == EOF) {
			status = wu_unexpected_eof;
			break;
		}

		if (seen == 1) {
			if (desc->type == pnm_plain_pbm
			|| desc->type == pnm_raw_pbm || desc->type == pnm_mtv) {
				status = wu_ok;
				break;
			}
		} else if (seen == 2) {
			status = wu_ok;
			break;
		}
		++seen;
	}

	if (status == wu_ok) {
		int c;
		while ((c = getc(desc->ifp)) != EOF) {
			if (c == '\n') {
				return setup_desc(desc);
			}
		}
		return wu_unexpected_eof;
	}
	return status;
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
const char next_char) {
	if (next_char == '\n') {
		desc->type = pnm_pam;
		return wu_ok;
	} else if (next_char == ' ') {
		char newline;
		const int read = fscanf(desc->ifp, "332%c", &newline);
		if (read == 1 && newline == '\n') {
			desc->type = pnm_xv_thumb;
			return wu_ok;
		} else if (read == EOF) {
			return wu_unexpected_eof;
		}
	}
	return wu_invalid_signature;
}

enum wu_error pnm_open_file(struct pnm_desc *desc, FILE *ifp,
const bool maybe_mtv) {
	*desc = (struct pnm_desc) {.ifp = ifp};

	char magic[2];
	const long pos = ftell(ifp);
	const int matches = fscanf(ifp, "P%2c", magic);
	if (matches == 1) {
		if (magic[0] == '7') {
			const enum wu_error st = disambiguate(desc, magic[1]);
			if (st != wu_ok) {
				return st;
			}
		} else {
			if ( (magic[0] == pnm_pgx && magic[1] != ' ')
			|| !isspace(magic[1])) {
				return wu_invalid_signature;
			}
			desc->type = (enum pnm_type)magic[0];
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
		case pnm_pgx:
			return wu_ok;
		case pnm_mtv: // Invalid here
			break;
		}
	} else if (matches == EOF) {
		return wu_unexpected_eof;
	} else if (maybe_mtv) {
		fseek(desc->ifp, pos, SEEK_SET);
		desc->type = pnm_mtv;
		return wu_ok;
	}
	return wu_invalid_signature;
}
