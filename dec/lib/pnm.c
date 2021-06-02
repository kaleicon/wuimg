#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <stdbool.h>
#include <math.h>

#include "../../common.h"
#include "common/unpack.h"
#include "common/text.h"
#include "pnm.h"

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
		uint32_t integer;
		float real;
	};

	const size_t dims = desc->w * desc->h * desc->ch;
	union int_real *out = malloc(dims * sizeof(*out));
	if (!out) {
		return NULL;
	}

	const size_t read = fread(out, sizeof(*out), dims, desc->ifp);
	if (read != dims) {
		puts(RASTER_EOF);
	}

	if (desc->pfm_scale == 1.0f) {
		loop_endian32(&out->integer, desc->pfm_endian, read);
	} else {
		for (size_t i = 0; i < read; ++i) {
			const union int_real val =
				{endian32(out[i].integer, desc->pfm_endian)};
			out[i].real = val.real * desc->pfm_scale;
		}
	}
	return (unsigned char *)out;
}

static unsigned char * xv_thumbnail_decode(const struct pnm_desc *desc) {
	const size_t data_size = desc->w * desc->h;
	size_t dims = data_size;
	if (desc->expand) {
		dims *= 3;
	}

	unsigned char *restrict output = malloc(dims);
	if (!output) {
		return NULL;
	}

	const size_t xv_off = dims - data_size;
	const size_t read = fread(output + xv_off, 1, data_size, desc->ifp);
	if (read != data_size) {
		puts(RASTER_EOF);
	}

	if (desc->expand) {
		strip_expand332(output, output + xv_off, read, 1, 1);
	}
	return output;
}

static unsigned char * raw_ppm_decode(const struct pnm_desc *desc) {
	const size_t dims = desc->w * desc->h * desc->ch;
	void *output = malloc(dims * desc->bytedepth);
	if (!output) {
		return NULL;
	}

	const size_t read = fread(output, desc->bytedepth, dims, desc->ifp);
	if (read != dims) {
		puts(RASTER_EOF);
	}

	if (desc->bytedepth == 2) {
		if (desc->maxval != USHRT_MAX) {
			scale_16(output, read, desc->maxval);
		} else {
			loop_endian16(output, big_endian, read);
		}
	} else if (desc->maxval != UCHAR_MAX) {
		scale_8(output, read, desc->maxval);
	}
	return output;
}

static size_t read_num(const char *restrict buf, uint_fast32_t *val) {
	size_t i = 0;
	while (isspace(buf[i])) {
		++i;
	}

	for (int k = 0; k < 6 && isdigit(buf[i]); ++k, ++i) {
		*val = *val * 10 + (uint_fast32_t)(buf[i] - '0');
	}
	return i;
}

static unsigned char * plain_ppm_decode(const struct pnm_desc *restrict desc) {
	const size_t dims = desc->w * desc->h * desc->ch;
	void *restrict output = malloc(dims * desc->bytedepth);
	if (!output) {
		return NULL;
	}

	struct text_block *text = new_text_block();
	if (!text) {
		free(output);
		return NULL;
	}

	const uint_fast32_t range = desc->maxval > UCHAR_MAX
		? USHRT_MAX : UCHAR_MAX;
	const uint_fast32_t scale = (range << 16) / desc->maxval + 1;

	size_t cnt = 0;
	do {
		const size_t end = read_spaced_text(text, desc->ifp);
		if (!end) {
			puts(RASTER_EOF);
			break;
		}

		size_t pos = 0;
		do {
			uint_fast32_t val = 0;
			pos += read_num(text->buf + pos, &val);
			if (val > desc->maxval) {
				puts(RASTER_INV);
				free(text);
				return output;
			}

			val = (val * scale) >> 16;
			if (desc->bytedepth == 2) {
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

static unsigned char * raw_pbm_decode(const struct pnm_desc *desc) {
	if (!desc->expand) {
		const size_t size = scanline_length(desc->w, 1, 1) * desc->h;
		unsigned char *data = malloc(size);
		if (data) {
			fread(data, 1, size, desc->ifp);
		}
		return data;
	}
	return strip_map_unpack(desc->ifp, desc->w, desc->h, 1,
		op_expand_invert, 1);
}

static unsigned char * plain_pbm_decode(const struct pnm_desc *desc) {
	const size_t dims = desc->w * desc->h;
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
				continue;
			case '0':
				output[cnt] = 0xff;
				++cnt;
				continue;
			case '1':
				output[cnt] = 0x00;
				++cnt;
				continue;
			}
			puts(RASTER_INV);
			free(buf);
			free(output);
			return NULL;
		}
	}
	free(buf);
	return output;
}

unsigned char * pnm_decode_next(const struct pnm_desc *desc) {
	switch (desc->type) {
	case plain_pbm:
		return plain_pbm_decode(desc);
	case raw_pbm:
		return raw_pbm_decode(desc);
	case plain_pgm:
	case plain_ppm:
		return plain_ppm_decode(desc);
	case raw_pgm:
	case raw_ppm:
	case pam:
	case mtv:
		return raw_ppm_decode(desc);
	case xv_thumb:
		return xv_thumbnail_decode(desc);
	case color_pfm:
	case gray_pfm:
		return pfm_decode(desc);
	}
	return NULL;
}

/* Header parsing */

static size_t count_images(struct pnm_desc *desc) {
	const size_t len = (size_t)file_get_remaining(desc->ifp);
	if (desc->type == raw_pbm) {
		return len / (scanline_length(desc->w, 1, 1) * desc->h);
	} else if (desc->type == xv_thumb) {
		return len / (desc->w * desc->h);
	} else {
		return len / (desc->w * desc->h * desc->ch) / desc->bytedepth;
	}
}

static enum lib_fail setup_desc(struct pnm_desc *desc) {
	if (!desc->w || !desc->h) {
		return lib_invalid_header;
	}

	switch (desc->type) {
	case plain_pbm: case raw_pbm: case mtv:
		desc->maxval = 255;
		desc->bytedepth = 1;
		break;
	case xv_thumb:
		if (desc->maxval != 255) {
			return lib_invalid_header;
		}
		desc->bytedepth = 1;
		break;
	case color_pfm: case gray_pfm:
		if (fpclassify(desc->pfm_scale) != FP_NORMAL) {
			return lib_invalid_header;
		}
		desc->bytedepth = 4;
		desc->pfm_endian = signbit(desc->pfm_scale)
			? little_endian : big_endian;
		desc->pfm_scale = fabsf(desc->pfm_scale);
		break;
	default:
		if (!desc->maxval) {
			return lib_invalid_header;
		}
		desc->bytedepth = desc->maxval > UCHAR_MAX ? 2 : 1;
	}

	switch (desc->type) {
	case plain_pbm: case plain_pgm: case raw_pbm: case raw_pgm: case gray_pfm:
		desc->ch = 1;
		break;
	case plain_ppm: case raw_ppm: case mtv: case xv_thumb: case color_pfm:
		desc->ch = 3;
		break;
	case pam:
		if (!desc->ch) {
			return lib_invalid_header;
		}
	}

	desc->nr = count_images(desc);
	if (!desc->nr) {
		return lib_unexpected_eof;
	}

	switch (desc->type) {
	case plain_pbm: case plain_pgm: case plain_ppm: case pam:
	case color_pfm: case gray_pfm:
		desc->nr = 1;
		break;
	default:
		break;
	}
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
			return lib_invalid_header;
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
		return read_pam_token(desc->ifp, " %zu%c", &desc->w, desc->w);
	} else if (!strcmp("HEIGHT", token)) {
		return read_pam_token(desc->ifp, " %zu%c", &desc->h, desc->h);
	} else if (!strcmp("DEPTH", token)) {
		return read_pam_token(desc->ifp, " %hhu%c", &desc->ch,
			desc->ch);
	} else if (!strcmp("MAXVAL", token)) {
		return read_pam_token(desc->ifp, " %hu%c", &desc->maxval,
			desc->maxval);
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
		const int scanned = fscanf(desc->ifp, "%8s", token);
		if (scanned == 1) {
			status = match_pam_token(desc, token, &finished);
			if (status != lib_ok) {
				return status;
			}
		} else if (scanned == EOF) {
			return lib_unexpected_eof;
		} else {
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
			result = fscanf(desc->ifp, "%zu", &desc->w);
		} else if (seen == 1) {
			result = fscanf(desc->ifp, "%zu", &desc->h);
		} else {
			if (desc->type == color_pfm || desc->type == gray_pfm) {
				result = fscanf(desc->ifp, "%f", &desc->pfm_scale);
			} else {
				result = fscanf(desc->ifp, "%hu", &desc->maxval);
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
		rewind(desc->ifp);
		desc->type = mtv;
		return lib_ok;
	}
	return lib_unknown_format;
}
