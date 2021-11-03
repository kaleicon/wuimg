#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <stdbool.h>
#include <math.h>

#include "../common.h"
#include "../raster/text.h"
#include "pnm.h"

const char * pnm_type_str(const enum pnm_type type) {
	switch (type) {
	case plain_pbm: return "Text PBM";
	case plain_pgm: return "Text PGM";
	case plain_ppm: return "Text PPM";
	case raw_pbm: return "Raw PBM";
	case raw_pgm: return "Raw PGM";
	case raw_ppm: return "Raw PPM";
	case pam: return "PAM";
	case xv_thumb: return "Xv thumb";
	case mtv: return "MTV";
	case color_pfm: return "Color PFM";
	case gray_pfm: return "Gray PFM";
	}
	return "???";
}

static void scale_16(unsigned short *output, const size_t dims,
const unsigned short maxval) {
	const uint_fast32_t scale = ((unsigned)USHRT_MAX << 16) / maxval + 1;
	for (size_t i = 0; i < dims; ++i) {
		uint_fast32_t val = endian16(output[i], big_endian);
		output[i] = (unsigned short)((val * scale) >> 16);
	}
}

static void scale_8(unsigned char *output, const size_t dims,
const unsigned short maxval) {
	const uint_fast16_t scale = ((unsigned)UCHAR_MAX << 8) / maxval + 1;
	for (size_t i = 0; i < dims; ++i) {
		output[i] = (unsigned char)((scale * output[i]) >> 8);

	}
}

static unsigned char * pfm_decode(const struct pnm_desc *desc) {
	union int_real {
		uint32_t bytes;
		float real;
	};

	const size_t dims = desc->rast.w * desc->rast.h * desc->rast.ch;
	union int_real *out = malloc(dims * sizeof(out->real));
	if (!out) {
		return NULL;
	}

	const size_t read = fread(out, sizeof(*out), dims, desc->ifp);
	if (read != dims) {
		puts(RASTER_EOF);
	}

	if (desc->scale.pfm == 1.0f) {
		loop_endian32(&out->bytes, desc->pfm_endian, read);
	} else {
		for (size_t i = 0; i < read; ++i) {
			const union int_real val =
				{.bytes = endian32(out[i].bytes, desc->pfm_endian)};
			out[i].real = val.real * desc->scale.pfm;
		}
	}
	return (unsigned char *)out;
}

static unsigned char * raw_ppm_decode(const struct pnm_desc *desc) {
	const size_t dims = desc->rast.w * desc->rast.ch * desc->rast.h;
	void *output = malloc(dims * desc->bytedepth);
	if (!output) {
		return NULL;
	}

	const size_t read = fread(output, desc->bytedepth, dims, desc->ifp);
	if (read != dims) {
		puts(RASTER_EOF);
	}

	switch (desc->bytedepth) {
	case 1:
		scale_8(output, read, desc->scale.pnm);
		break;
	case 2:
		if (desc->scale.pnm == USHRT_MAX) {
			loop_endian16(output, big_endian, read);
		} else {
			scale_16(output, read, desc->scale.pnm);
		}
		break;
	}
	return output;
}

static unsigned char * plain_ppm_decode(const struct pnm_desc *restrict desc) {
	const size_t dims = desc->rast.w * desc->rast.h * desc->rast.ch;
	void *restrict output = malloc(dims * desc->bytedepth);
	if (!output) {
		return NULL;
	}

	struct text_block *text = text_block_new();
	if (!text) {
		free(output);
		return NULL;
	}

	const text_fast_t range = (desc->scale.pnm > UCHAR_MAX)
		? USHRT_MAX : UCHAR_MAX;
	const text_fast_t scale = (range << 16) / desc->scale.pnm + 1;
	const size_t digits = (desc->scale.pnm > UCHAR_MAX) ? 5 : 3;

	size_t cnt = 0;
	do {
		const size_t end = text_block_read_spaced(text, desc->ifp);
		if (!end) {
			puts(RASTER_EOF);
			break;
		}

		size_t pos = 0;
		do {
			text_fast_t val;
			pos += text_read_uint(text->buf + pos, &val, digits);
			if (val > desc->scale.pnm) {
				puts(RASTER_INV);
				free(text);
				return output;
			}

			val = (val * scale) >> 16;
			if (desc->rast.bitdepth == 16) {
				unsigned short *out = output;
				out[cnt] = (unsigned short)(val);
			} else {
				unsigned char *out = output;
				out[cnt] = (unsigned char)(val);
			}
			++cnt;
			if (!isspace(text->buf[pos]) && cnt < dims) {
				puts(RASTER_INV);
				free(text);
				return output;
			}
		} while (pos < end && cnt < dims);
	} while (cnt < dims);
	free(text);
	return output;
}

static unsigned char * plain_pbm_decode(const struct pnm_desc *desc) {
	const size_t dims = desc->rast.w * desc->rast.h;
	unsigned char *output = malloc(dims);
	if (!output) {
		return NULL;
	}

	unsigned char *buf = malloc(BUFSIZ);
	if (!buf) {
		free(output);
		return NULL;
	}

	size_t cnt = 0;
	while (cnt < dims) {
		const size_t read = fread(buf, 1, BUFSIZ, desc->ifp);
		if (read == 0) {
			puts(RASTER_EOF);
			break;
		}

		for (size_t i = 0; i < read && cnt < dims; ++i) {
			switch (buf[i]) {
			case '\t': case '\n': case '\v': case '\f': case '\r':
			case ' ':
				break;
			case '0':
				output[cnt] = 0xff;
				++cnt;
				break;
			case '1':
				output[cnt] = 0x00;
				++cnt;
				break;
			default:
				puts(RASTER_INV);
				free(buf);
				free(output);
				return NULL;
			}
		}
	}
	free(buf);
	return output;
}

unsigned char * pnm_decode_next(const struct pnm_desc *desc) {
	switch (desc->type) {
	case plain_pbm:
		return plain_pbm_decode(desc);
	case plain_pgm:
	case plain_ppm:
		return plain_ppm_decode(desc);
	case raw_pgm:
	case raw_ppm:
	case pam:
		if (desc->scale.pnm != UCHAR_MAX) {
			return raw_ppm_decode(desc);
		}
		break;
	case color_pfm:
	case gray_pfm:
		return pfm_decode(desc);
	case raw_pbm:
	case xv_thumb:
	case mtv:
		break;
	}
	return lib_load_rast(desc->ifp, &desc->rast);
}

/* Header parsing */

static size_t count_images(struct pnm_desc *desc) {
	const size_t len = (size_t)file_get_remaining(desc->ifp);
	return len / raster_size(&desc->rast);
}

static enum lib_fail setup_desc(struct pnm_desc *desc) {
	if (!desc->rast.w || !desc->rast.h) {
		return lib_invalid_header;
	}

	switch (desc->type) {
	case raw_pbm:
		desc->rast.bitdepth = 1;
		desc->rast.attr = pix_inverted;
		break;
	case plain_pbm:
	case mtv:
		desc->rast.bitdepth = 8;
		break;
	case xv_thumb:
		if (desc->scale.pnm != 255) {
			return lib_invalid_header;
		}
		desc->rast.bitdepth = 8;
		desc->rast.attr = pix_packing_332;
		break;
	case color_pfm: case gray_pfm:
		if (fpclassify(desc->scale.pfm) != FP_NORMAL) {
			return lib_invalid_header;
		}
		desc->rast.bitdepth = 32;
		desc->rast.attr = pix_float;
		desc->pfm_endian = signbit(desc->scale.pfm)
			? little_endian : big_endian;
		desc->scale.pfm = fabsf(desc->scale.pfm);
		break;
	case plain_pgm: case plain_ppm: case raw_pgm: case raw_ppm: case pam:
		if (!desc->scale.pnm) {
			return lib_invalid_header;
		}
		desc->rast.bitdepth = desc->scale.pnm > UCHAR_MAX ? 16 : 8;
	}

	switch (desc->type) {
	case plain_pbm: case plain_pgm: case raw_pbm: case raw_pgm: case gray_pfm:
	case xv_thumb:
		desc->rast.ch = 1;
		break;
	case plain_ppm: case raw_ppm: case mtv: case color_pfm:
		desc->rast.ch = 3;
		break;
	case pam:
		if (!desc->rast.ch) {
			return lib_invalid_header;
		}
	}

	raster_normalize(&desc->rast);

	switch (desc->type) {
	case plain_pbm: case plain_pgm: case plain_ppm:
		desc->nr = 1;
		break;
	case raw_pbm: case raw_pgm: case raw_ppm:
		desc->nr = count_images(desc);
		if (!desc->nr) {
			return lib_unexpected_eof;
		}
		break;
	default:
		if (!count_images(desc)) {
			return lib_unexpected_eof;
		}
		desc->nr = 1;
		break;
	}
	desc->bytedepth = desc->rast.bitdepth / 8;
	return lib_ok;
}

static enum lib_fail read_pam_token(FILE *ifp, const char *fmt,
void *where, const bool where_val) {
	if (!where_val) {
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
		skip_line(desc->ifp);
		return lib_ok;
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
		skip_line(desc->ifp);
		return lib_ok;
	} else if (!strcmp("ENDHDR", token)) {
		const int c = getc(desc->ifp);
		if (c != '\n') {
			if (c == EOF) {
				return lib_unexpected_eof;
			}
			return lib_invalid_header;
		}
		*finished = true;
		return lib_ok;
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
			if (desc->type == color_pfm || desc->type == gray_pfm) {
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
			if (desc->type == plain_pbm || desc->type == raw_pbm
			|| desc->type == mtv) {
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
	switch (desc->type) {
	case plain_pbm:
	case plain_pgm:
	case plain_ppm:
	case raw_pbm:
	case raw_pgm:
	case raw_ppm:
	case xv_thumb:
	case mtv:
	case color_pfm:
	case gray_pfm:
		return parse_any_map(desc);
	case pam:
		return parse_arbitrary_map(desc);
	}
	return lib_unknown_format;
}

static enum lib_fail disambiguate(struct pnm_desc *desc,
const char next_char) {
	if (next_char == '\n') {
		desc->type = pam;
		return lib_ok;
	} else if (next_char == ' ') {
		char newline;
		const int read = fscanf(desc->ifp, "332%c", &newline);
		if (read == 1 && newline == '\n') {
			desc->type = xv_thumb;
			return lib_ok;
		} else if (read == EOF) {
			return lib_unexpected_eof;
		}
	}
	return lib_unknown_format;
}

enum lib_fail pnm_open_file(FILE *ifp, struct pnm_desc *desc,
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
				return lib_unknown_format;
			}
			desc->type = (enum pnm_type)magic[0];
		}

		switch (desc->type) {
		case plain_pbm:
		case plain_pgm:
		case plain_ppm:
		case raw_pbm:
		case raw_pgm:
		case raw_ppm:
		case pam:
		case xv_thumb:
		case color_pfm:
		case gray_pfm:
			return lib_ok;
		case mtv: // Invalid here
			break;
		}
	} else if (matches == EOF) {
		return lib_unexpected_eof;
	} else if (maybe_mtv) {
		fseek(desc->ifp, pos, SEEK_SET);
		desc->type = mtv;
		return lib_ok;
	}
	return lib_unknown_format;
}
