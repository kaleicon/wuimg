#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <math.h>

#include "../common.h"
#include "../raster/other.h"
#include "pnm.h"

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
	case pnm_mtv: return "MTV";
	case pnm_color_pfm: return "Color PFM";
	case pnm_gray_pfm: return "Gray PFM";
	}
	return "???";
}

static void scale_16(unsigned short *dst, const size_t dims,
const unsigned short maxval) {
	const uint_fast32_t scale = ((unsigned)USHRT_MAX << 16) / maxval + 1;
	for (size_t i = 0; i < dims; ++i) {
		uint_fast32_t val = endian16(dst[i], big_endian);
		dst[i] = (unsigned short)((val * scale) >> 16);
	}
}

static void scale_8(unsigned char *dst, const size_t dims,
const unsigned short maxval) {
	const uint_fast16_t scale = ((unsigned)UCHAR_MAX << 8) / maxval + 1;
	for (size_t i = 0; i < dims; ++i) {
		dst[i] = (unsigned char)((scale * dst[i]) >> 8);

	}
}

static size_t pfm_decode(const struct pnm_desc *desc,
const size_t dims, union int_real *out) {
	const size_t read = fread(out, sizeof(*out), dims, desc->ifp);
	if (desc->scale.pfm == 1.0f) {
		loop_endian32(&out->bytes, desc->pfm_endian, read);
	} else {
		for (size_t i = 0; i < read; ++i) {
			const union int_real val =
				{.bytes = endian32(out[i].bytes, desc->pfm_endian)};
			out[i].real = val.real * desc->scale.pfm;
		}
	}
	return read;
}

static size_t raw_ppm_decode(const struct pnm_desc *restrict desc,
const size_t dims, void *restrict dst) {
	const size_t read = fread(dst, desc->bytedepth, dims, desc->ifp);
	switch (desc->bytedepth) {
	case 1:
		scale_8(dst, read, desc->scale.pnm);
		break;
	case 2:
		if (desc->scale.pnm == USHRT_MAX) {
			loop_endian16(dst, big_endian, read);
		} else {
			scale_16(dst, read, desc->scale.pnm);
		}
		break;
	}
	return read;
}

static size_t plain_ppm_decode(const struct pnm_desc *restrict desc,
const size_t dims, void *restrict dst) {
	struct text_block *text = text_block_new();
	if (!text) {
		return 0;
	}

	const mem_fast_t range = (desc->scale.pnm > UCHAR_MAX)
		? USHRT_MAX : UCHAR_MAX;
	const mem_fast_t scale = (range << 16) / desc->scale.pnm + 1;
	const size_t digits = (desc->scale.pnm > UCHAR_MAX) ? 5 : 3;

	size_t cnt = 0;
	do {
		const size_t end = text_block_read_spaced(text, desc->ifp);
		if (!end) {
			break;
		}

		size_t pos = 0;
		do {
			mem_fast_t val;
			pos += text_read_uint(text->buf + pos, &val, digits);
			if (val > desc->scale.pnm) {
				free(text);
				return cnt;
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
			if (!isspace(text->buf[pos]) && cnt < dims) {
				free(text);
				return cnt;
			}
		} while (pos < end && cnt < dims);
	} while (cnt < dims);
	free(text);
	return cnt;
}

static size_t plain_pbm_decode(const struct pnm_desc *restrict desc,
const size_t dims, unsigned char *restrict dst) {
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
	const size_t elems = desc->rast.w * desc->rast.h * desc->rast.ch;
	const size_t size = elems * desc->bytedepth;
	fseek(desc->ifp, desc->data_start + (long)(size * i), SEEK_SET);

	switch (desc->type) {
	case pnm_plain_pbm:
		return plain_pbm_decode(desc, elems, dst);
	case pnm_plain_pgm:
	case pnm_plain_ppm:
		return plain_ppm_decode(desc, elems, dst);
	case pnm_raw_pgm:
	case pnm_raw_ppm:
	case pnm_pam:
		if (desc->scale.pnm != UCHAR_MAX) {
			return raw_ppm_decode(desc, elems, dst);
		}
		break;
	case pnm_color_pfm:
	case pnm_gray_pfm:
		return pfm_decode(desc, elems, dst);
	case pnm_raw_pbm:
	case pnm_xv_thumb:
	case pnm_mtv:
		break;
	}
	return fread(dst, desc->bytedepth, elems, desc->ifp);
}

/* Header parsing */

static size_t count_images(struct pnm_desc *desc) {
	fseek(desc->ifp, 0, SEEK_END);
	const long len = ftell(desc->ifp) - desc->data_start;
	if (len > 0) {
		return zumax(1, (size_t)len / raster_size(&desc->rast));
	}
	return 0;
}

static enum lib_fail setup_desc(struct pnm_desc *desc) {
	if (!desc->rast.w || !desc->rast.h) {
		return lib_invalid_header;
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
			return lib_invalid_header;
		}
		desc->rast.bitdepth = 8;
		desc->rast.attr = pix_packing_332;
		break;
	case pnm_color_pfm: case pnm_gray_pfm:
		if (fpclassify(desc->scale.pfm) != FP_NORMAL) {
			return lib_invalid_header;
		}
		desc->rast.bitdepth = 32;
		desc->rast.attr = pix_float;
		desc->pfm_endian = signbit(desc->scale.pfm)
			? little_endian : big_endian;
		desc->scale.pfm = fabsf(desc->scale.pfm);
		break;
	case pnm_plain_pgm: case pnm_plain_ppm:
	case pnm_raw_pgm: case pnm_raw_ppm:
	case pnm_pam:
		if (!desc->scale.pnm) {
			return lib_invalid_header;
		}
		desc->rast.bitdepth = desc->scale.pnm > UCHAR_MAX ? 16 : 8;
	}

	switch (desc->type) {
	case pnm_plain_pbm: case pnm_plain_pgm:
	case pnm_raw_pbm: case pnm_raw_pgm: case pnm_gray_pfm:
	case pnm_xv_thumb:
		desc->rast.ch = 1;
		break;
	case pnm_plain_ppm: case pnm_raw_ppm: case pnm_mtv: case pnm_color_pfm:
		desc->rast.ch = 3;
		break;
	case pnm_pam:
		if (!desc->rast.ch) {
			return lib_invalid_header;
		}
	}

	raster_normalize(&desc->rast);
	desc->bytedepth = desc->rast.bitdepth / 8;
	desc->data_start = ftell(desc->ifp);

	switch (desc->type) {
	case pnm_raw_pbm: case pnm_raw_pgm: case pnm_raw_ppm:
		desc->nr = count_images(desc);
		if (!desc->nr) {
			return lib_unexpected_eof;
		}
		break;
	default:
		desc->nr = 1;
		break;
	}
	return lib_ok;
}

static enum lib_fail skip_line(FILE *ifp) {
	for (;;) {
		switch (getc(ifp)) {
		case '\n': return lib_ok;
		case EOF: return lib_unexpected_eof;
		}
	}
	return lib_unexpected_eof;
}

static enum lib_fail read_pam_token(FILE *ifp, const char *fmt,
void *where, const bool cur_val) {
	if (!cur_val) {
		unsigned char newline;
		switch (fscanf(ifp, fmt, where, &newline)) {
		case 2:
			if (newline == '\n') {
				return lib_ok;
			}
			break;
		case EOF:
			return lib_unexpected_eof;
		}
	}
	return lib_invalid_header;
}

static enum lib_fail match_pam_token(struct pnm_desc *desc, const char *token,
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
		case EOF: return lib_unexpected_eof;
		case '\n':
			*finished = true;
			return lib_ok;
		}
	}
	return lib_invalid_header;
}

static enum lib_fail parse_arbitrary_map(struct pnm_desc *desc) {
	char token[9];
	bool finished = false;
	enum lib_fail status;
	while (!finished) {
		switch(fscanf(desc->ifp, "%8s", token)) {
		case 1:
			status = match_pam_token(desc, token, &finished);
			if (status != lib_ok) {
				return status;
			}
			break;
		case EOF:
			return lib_unexpected_eof;
		default:
			return lib_invalid_header;
		}
	}
	return setup_desc(desc);
}

static enum lib_fail skip_any_junk(struct pnm_desc *desc) {
	bool comment = false;
	for (;;) {
		const int c = getc(desc->ifp);
		if (!comment) {
			if (c == '#') {
				comment = true;
			} else if (isspace(c)) {
				continue;
			} else if (isdigit(c) || c == '-') {
				ungetc(c, desc->ifp);
				return lib_ok;
			} else {
				return lib_invalid_header;
			}
		} else if (c == '\n') {
			comment = false;
		} else if (c == EOF) {
			return lib_unexpected_eof;
		}
	}
}

static enum lib_fail parse_any_map(struct pnm_desc *desc) {
	// Comments may appear at any point
	int seen = 0;
	enum lib_fail status;
	while ((status = skip_any_junk(desc)) == lib_ok) {
		int result;
		if (seen == 0) {
			result = fscanf(desc->ifp, "%zu", &desc->rast.w);
		} else if (seen == 1) {
			result = fscanf(desc->ifp, "%zu", &desc->rast.h);
		} else {
			if (desc->type == pnm_color_pfm || desc->type == pnm_gray_pfm) {
				result = fscanf(desc->ifp, "%f", &desc->scale.pfm);
			} else {
				result = fscanf(desc->ifp, "%hu", &desc->scale.pnm);
			}
		}

		if (result == 0) {
			status = lib_invalid_header;
			break;
		} else if (result == EOF) {
			status = lib_unexpected_eof;
			break;
		}

		if (seen == 1) {
			if (desc->type == pnm_plain_pbm
			|| desc->type == pnm_raw_pbm || desc->type == pnm_mtv) {
				status = lib_ok;
				break;
			}
		} else if (seen == 2) {
			status = lib_ok;
			break;
		}
		++seen;
	}

	if (status == lib_ok) {
		int c;
		while ((c = getc(desc->ifp)) != EOF) {
			if (c == '\n') {
				return setup_desc(desc);
			}
		}
		return lib_unexpected_eof;
	}
	return status;
}

enum lib_fail pnm_parse_header(struct pnm_desc *desc) {
	if (desc->type == pnm_pam) {
		return parse_arbitrary_map(desc);
	}
	return parse_any_map(desc);
}

static enum lib_fail disambiguate(struct pnm_desc *desc,
const char next_char) {
	if (next_char == '\n') {
		desc->type = pnm_pam;
		return lib_ok;
	} else if (next_char == ' ') {
		char newline;
		const int read = fscanf(desc->ifp, "332%c", &newline);
		if (read == 1 && newline == '\n') {
			desc->type = pnm_xv_thumb;
			return lib_ok;
		} else if (read == EOF) {
			return lib_unexpected_eof;
		}
	}
	return lib_invalid_signature;
}

enum lib_fail pnm_open_file(struct pnm_desc *desc, FILE *ifp,
const bool maybe_mtv) {
	memset(desc, 0, sizeof(*desc));
	desc->ifp = ifp;

	char magic[2];
	const long pos = ftell(ifp);
	const int matches = fscanf(ifp, "P%2c", magic);
	if (matches == 1) {
		if (magic[0] == '7') {
			const enum lib_fail st = disambiguate(desc, magic[1]);
			if (st != lib_ok) {
				return st;
			}
		} else {
			if (!isspace(magic[1])) {
				return lib_invalid_signature;
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
			return lib_ok;
		case pnm_mtv: // Invalid here
			break;
		}
	} else if (matches == EOF) {
		return lib_unexpected_eof;
	} else if (maybe_mtv) {
		fseek(desc->ifp, pos, SEEK_SET);
		desc->type = pnm_mtv;
		return lib_ok;
	}
	return lib_invalid_signature;
}
