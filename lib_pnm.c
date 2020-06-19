#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <stdbool.h>

#include "common.h"
#include "common_unpack.h"
#include "lib_pnm.h"

const char PNM_EOF[] = "PNM Warning: Unexpected End Of File. Output may contain "
	"garbage.";

const char * pnm_fail_string(const enum pnm_fail fail) {
	switch (fail) {
	case pnm_ok:
		return "PNM: All OK";
	case pnm_unexpected_eof:
		return "PNM: Unexpected End Of File";
	case pnm_unknown_format:
		return "PNM: Not a known PNM file";
	case pnm_invalid_header:
		return "PNM: Invalid header";
	case pnm_unsupported_tuple:
		return "PNM: PAM: Found unknown TUPLTYPE";
	case pnm_alloc_error:
		return "PNM: Alloc error";
	}
	return "PNM: I just don't know what went wrong";
}

static void scale_16(u_int8_t *output, const size_t dims,
const unsigned short maxval, const bool swap) {
	const u_int32_t scale = ((unsigned)USHRT_MAX << 16) / maxval + 1;
	for (size_t i = 0; i < dims; i += 2) {
		u_int32_t val = endian_u16(output + i, big_endian);
		val *= scale;
		if (swap) {
			output[i] = (u_int8_t)(val >> 16);
			output[i+1] = (u_int8_t)(val >> 24);
		} else {
			output[i] = (u_int8_t)(val >> 24);
			output[i+1] = (u_int8_t)(val >> 16);
		}
	}
}

static void scale_8(u_int8_t *output, const size_t dims,
const unsigned short maxval) {
	const u_int32_t scale = ((unsigned)UCHAR_MAX << 8) / maxval + 1;
	for (size_t i = 0; i < dims; ++i) {
		output[i] = (u_int8_t)((scale * output[i]) >> 8);
	}
}

static unsigned char * xv_thumbnail_decode(const struct pnm_desc *desc) {
	const size_t data_size = desc->w * desc->h;
	size_t dims;
	if (desc->xv_no_expand) {
		dims = data_size;
	} else {
		dims = data_size * 3;
	}

	unsigned char *restrict output = malloc(dims);
	if (!output) {
		return NULL;
	}

	const size_t xv_off = dims - data_size;
	const size_t read = fread(output + xv_off, 1, data_size, desc->ifp);
	if (read != data_size) {
		puts(PNM_EOF);
	}

	if (desc->xv_no_expand) {
		return output;
	} else {
		const unsigned char *restrict src = output + xv_off;
		const int scale = (UCHAR_MAX << 16) / 0x07 + 1;

		for (size_t i = 0; i < read * 3; ++i) {
			const int r = src[i] >> 5;
			const int g = (src[i] >> 2) & 0x07;
			const int b = src[i] & 0x03;

			output[i*3] = (unsigned char)((r * scale) >> 16);
			output[i*3 + 1] = (unsigned char)((g * scale) >> 16);
			output[i*3 + 2] = (unsigned char)(b * 0xff/3);
		}
		return output;
	}
}

static unsigned char * raw_ppm_decode(const struct pnm_desc *desc) {
	const size_t dims = desc->w * desc->h * desc->ch * desc->bytedepth;
	void *output = malloc(dims);
	if (!output) {
		return NULL;
	}

	const size_t read = fread(output, 1, dims, desc->ifp);
	if (read != dims) {
		puts(PNM_EOF);
	}

	if (desc->bytedepth == 2) {
		if (desc->maxval != USHRT_MAX) {
			scale_16(output, read, desc->maxval, desc->swap);
		} else if (desc->swap) {
			swap_u16_inplace(output, read);
		}
	} else if (desc->maxval != UCHAR_MAX) {
		scale_8(output, read, desc->maxval);
	}
	return output;
}

static u_int32_t read_num(const unsigned char *restrict buf,
size_t *restrict parsed, const unsigned maxval) {
	size_t i = 0;
	while (isspace(buf[i])) {
		++i;
	}
	u_int32_t val = 0;
	for (int k = 0; k < 6; ++k) { // max decimal length for u_int16
		if (!isdigit(buf[i])) {
			break;
		}
		val = val*10 + buf[i] - '0';
		++i;
	}

	if (isspace(buf[i]) && val <= maxval) {
		*parsed = i + 1;
	} else {
		*parsed = 0;
	}
	return val;
}

static size_t find_end(unsigned char *buf, const size_t size) {
	size_t i = size - 1;
	while (isdigit(buf[i]) && i) {
		--i;
	}
	return i;
}

static unsigned char * plain_ppm_decode(const struct pnm_desc *restrict desc) {
	const size_t dims = desc->w * desc->h * desc->ch * desc->bytedepth;
	unsigned char *restrict output = malloc(dims);
	if (!output) {
		return NULL;
	}

	unsigned char *restrict buf = malloc(BUFSIZ);
	if (!buf) {
		free(output);
		return NULL;
	}

	const u_int32_t range = desc->maxval > UCHAR_MAX ? USHRT_MAX : UCHAR_MAX;
	const u_int32_t scale = (range << 16) / desc->maxval + 1;

	size_t end;
	size_t tail = 0;
	size_t cnt = 0;
	do {
		size_t left = BUFSIZ - tail;
		const size_t read = fread(buf + tail, 1, left, desc->ifp);
		if (read == left) {
			end = find_end(buf, BUFSIZ);
		} else if (!read) {
			puts(PNM_EOF);
			break;
		} else {
			end = read;
			buf[end] = ' ';
		}

		size_t pos = 0;
		do {
			size_t parsed;
			u_int32_t val = read_num(buf + pos, &parsed, desc->maxval);
			if (!parsed) {
				puts("PNM Error: Invalid data found while decoding.");
				free(buf);
				free(output);
				return NULL;
			}

			val *= scale;
			if (desc->bytedepth == 2) {
				if (desc->swap) {
					output[cnt] = (unsigned char)(val >> 16);
					output[cnt + 1] = (unsigned char)(val >> 24);
				} else {
					output[cnt] = (unsigned char)(val >> 24);
					output[cnt + 1] = (unsigned char)(val >> 16);
				}
			} else {
				output[cnt] = (unsigned char)(val >> 16);
			}
			cnt += desc->bytedepth;
			pos += parsed;
		} while (pos < end && cnt < dims);

		if (read == left) {
			tail = BUFSIZ - end - 1; // 'end' points to whitespace
			memcpy(buf, buf + end + 1, tail);
		} else {
			tail = 0;
		}
	} while (cnt < dims);
	free(buf);
	return output;
}

static unsigned char * raw_pbm_decode(const struct pnm_desc *desc) {
	const size_t dims = desc->w * desc->h;
	unsigned char *output = malloc(dims);
	if (!output) {
		return NULL;
	}

	const size_t data_size = scanline_length(desc->w, 1, 1) * desc->h;
	unsigned char *pbm_loc = output + dims - data_size;
	const size_t read = fread(pbm_loc, 1, data_size, desc->ifp);
	if (read != data_size) {
		puts(PNM_EOF);
	}

	strip_invert1(output, pbm_loc, desc->w, desc->h, 1);
	return output;
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
			puts(PNM_EOF);
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
			puts("PNM Error: Invalid data found while decoding.");
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
	}
	return NULL;
}

/* Header parsing */

static size_t count_images(struct pnm_desc *desc) {
	const long start = ftell(desc->ifp);
	fseek(desc->ifp, 0, SEEK_END);
	const long end = ftell(desc->ifp);
	fseek(desc->ifp, start, SEEK_SET);

	const size_t len = (size_t)(end - start);
	if (desc->type == raw_pbm) {
		return len / (scanline_length(desc->w, 1, 1) * desc->h);
	} else if (desc->type == xv_thumb) {
		return len / (desc->w * desc->h);
	} else {
		return len / (desc->w * desc->h * desc->ch) / desc->bytedepth;
	}
}

static enum pnm_fail setup_desc(struct pnm_desc *desc) {
	if (!desc->w || !desc->h) {
		return pnm_invalid_header;
	}

	switch (desc->type) {
	case plain_pbm: case plain_pgm: case raw_pbm: case raw_pgm:
		desc->ch = 1;
		break;
	case plain_ppm: case raw_ppm: case mtv: case xv_thumb:
		desc->ch = 3;
		break;
	case pam:
		if (!desc->ch) {
			return pnm_invalid_header;
		}
	}

	switch (desc->type) {
	case plain_pbm: case raw_pbm: case mtv:
		desc->maxval = 255;
		desc->bytedepth = 1;
		break;
	case xv_thumb:
		if (desc->maxval != 255) {
			return pnm_invalid_header;
		}
		desc->bytedepth = 1;
		break;
	default:
		if (!desc->maxval) {
			return pnm_invalid_header;
		}
		desc->bytedepth = desc->maxval > UCHAR_MAX ? 2 : 1;
	}

	desc->nr = count_images(desc);
	if (!desc->nr) {
		return pnm_unexpected_eof;
	}

	switch (desc->type) {
	case plain_pbm: case plain_pgm: case plain_ppm: case pam:
		desc->nr = 1;
		break;
	default:
		break;
	}
	return pnm_ok;
}

static void skip_line(FILE *ifp) {
	for (;;) {
		const int c = getc(ifp);
		if (c == '\n' || c == EOF) {
			return;
		}
	}
}

static enum pnm_fail match_pam_tupltype(FILE *ifp) {
	const char *valid_tuples[] = {"BLACKANDWHITE", "GRAYSCALE", "RGB",
		"BLACKANDWHITE_ALPHA", "GRAYSCALE_ALPHA", "RGB_ALPHA"};
	const size_t nr = sizeof(valid_tuples) / sizeof(valid_tuples[0]);
	char tuple[20];
	char newline;
	if (fscanf(ifp, " %19s%c", tuple, &newline) == 2) {
		if (newline != '\n') {
			return pnm_unsupported_tuple;
		}

		for (size_t i = 0; i < nr; ++i) {
			if (!strcmp(tuple, valid_tuples[i])) {
				return pnm_ok;
			}
		}
		return pnm_unsupported_tuple;
	}
	return pnm_unexpected_eof;
}

static enum pnm_fail read_pam_token(FILE *ifp, const char *fmt,
void *where, const size_t where_val) {
	if (!where_val) {
		unsigned char newline;
		switch (fscanf(ifp, fmt, where, &newline)) {
		case 2:
			if (newline == '\n') {
				return pnm_ok;
			}
			return pnm_invalid_header;
		case EOF:
			return pnm_unexpected_eof;
		}
	}
	return pnm_invalid_header;
}

static enum pnm_fail match_pam_token(struct pnm_desc *desc, const char *token,
bool *has_tuple, bool *finished) {
	if (token[0] == '#') {
		skip_line(desc->ifp);
		return pnm_ok;
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
		if (*has_tuple == false) {
			*has_tuple = true;
			return match_pam_tupltype(desc->ifp);
		}
		return pnm_unsupported_tuple;
	} else if (!strcmp("ENDHDR", token)) {
		const int c = getc(desc->ifp);
		if (c != '\n') {
			if (c == EOF) {
				return pnm_unexpected_eof;
			}
			return pnm_invalid_header;
		}
		*finished = true;
		return pnm_ok;
	}
	return pnm_invalid_header;
}

static enum pnm_fail parse_arbitrary_map(struct pnm_desc *desc) {
	char token[9];
	bool has_tuple = false;
	bool finished = false;
	enum pnm_fail status;
	while (!finished) {
		const int scanned = fscanf(desc->ifp, "%8s", token);
		if (scanned == 1) {
			status = match_pam_token(desc, token, &has_tuple,
				&finished);
			if (status != pnm_ok) {
				return status;
			}
		} else if (scanned == EOF) {
			return pnm_unexpected_eof;
		} else {
			return pnm_invalid_header;
		}
	}
	return setup_desc(desc);
}

static enum pnm_fail skip_any_junk(struct pnm_desc *desc) {
	bool comment = false;
	for (;;) {
		const int c = getc(desc->ifp);
		if (!comment) {
			if (c == '#') {
				comment = true;
			} else if (isspace(c)) {
				continue;
			} else if (isdigit(c)) {
				ungetc(c, desc->ifp);
				return pnm_ok;
			} else {
				return pnm_invalid_header;
			}
		} else if (c == '\n') {
			comment = false;
		} else if (c == EOF) {
			return pnm_unexpected_eof;
		}
	}
}

static enum pnm_fail parse_any_map(struct pnm_desc *desc) {
	// Comments may appear at any point
	int seen = 0;
	enum pnm_fail status;
	while ((status = skip_any_junk(desc)) == pnm_ok) {
		int result;
		if (seen == 0) {
			result = fscanf(desc->ifp, "%zu", &desc->w);
		} else if (seen == 1) {
			result = fscanf(desc->ifp, "%zu", &desc->h);
		} else {
			result = fscanf(desc->ifp, "%hu", &desc->maxval);
		}

		if (result == 0) {
			status = pnm_invalid_header;
			break;
		} else if (result == EOF) {
			status = pnm_unexpected_eof;
			break;
		}

		if (seen == 1) {
			if (desc->type == plain_pbm || desc->type == raw_pbm
			|| desc->type == mtv) {
				status = pnm_ok;
				break;
			}
		} else if (seen == 2) {
			status = pnm_ok;
			break;
		}
		++seen;
	}

	if (status == pnm_ok) {
		int c;
		while ((c = getc(desc->ifp)) != EOF) {
			if (c == '\n') {
				return setup_desc(desc);
			}
		}
		return pnm_unexpected_eof;
	}
	return status;
}

enum pnm_fail pnm_parse_header(struct pnm_desc *desc) {
	switch (desc->type) {
	case plain_pbm:
	case plain_pgm:
	case plain_ppm:
	case raw_pbm:
	case raw_pgm:
	case raw_ppm:
	case mtv:
	case xv_thumb:
		return parse_any_map(desc);
	case pam:
		return parse_arbitrary_map(desc);
	}
	return pnm_unknown_format;
}

static enum pnm_fail disambiguate(struct pnm_desc *desc,
const char next_char) {
	if (next_char == '\n') {
		desc->type = pam;
		return pnm_ok;
	} else if (next_char == ' ') {
		char newline;
		const int read = fscanf(desc->ifp, "332%c", &newline);
		if (read == 1 && newline == '\n') {
			desc->type = xv_thumb;
			return pnm_ok;
		} else if (read == EOF) {
			return pnm_unexpected_eof;
		}
	}
	return pnm_unknown_format;
}

enum pnm_fail pnm_open_file(FILE *ifp, struct pnm_desc *desc,
const bool maybe_mtv) {
	memset(desc, 0, sizeof(*desc));
	desc->ifp = ifp;

	char magic[2];
	const int matches = fscanf(ifp, "P%2c", magic);
	if (matches == 1) {
		if (magic[0] == '7') {
			const enum pnm_fail st = disambiguate(desc, magic[1]);
			if (st != pnm_ok) {
				return st;
			}
		} else {
			if (!isspace(magic[1])) {
				return pnm_unknown_format;
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
			return pnm_ok;
		case mtv: // Invalid here
			break;
		}
	} else if (matches == EOF) {
		return pnm_unexpected_eof;
	} else if (maybe_mtv) {
		desc->type = mtv;
		return pnm_ok;
	}
	return pnm_unknown_format;
}
