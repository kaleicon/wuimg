#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "../../common.h"
#include "common/unpack.h"
#include "common/text.h"

#include "xbm.h"

#define MAX_NAMELEN 64

void xbm_cleanup(struct xbm_desc *desc) {
	free(desc->name);
}

static int toxint(const int digit) {
	switch (digit) {
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;

	case 'A': case 'a': return 0xa;
	case 'B': case 'b': return 0xb;
	case 'C': case 'c': return 0xc;
	case 'D': case 'd': return 0xd;
	case 'E': case 'e': return 0xe;
	case 'F': case 'f': return 0xf;
	}
	return -1;
}

static size_t read_hex_num(const char *restrict buf, int_fast32_t *val,
const int_fast32_t alignment) {
	size_t i = 0;
	while (isspace(buf[i])) {
		++i;
	}

	if (buf[i] == '0' && (buf[i+1] == 'x' || buf[i+1] == 'X')) {
		i += 2;
		*val = 0;
		for (int_fast32_t k = 0; k < alignment * 2; ++k) {
			const int_fast32_t digit = toxint(buf[i]);
			if (digit == -1) {
				break;
			}
			*val = (*val << 4) + digit;
			++i;
		}
		return i;
	}
	*val = -1;
	return i;
}

static void decode_loop(struct text_block *text, FILE *ifp,
unsigned char *restrict output, const size_t dims, const int_fast32_t align) {
	size_t cnt = 0;
	do {
		const size_t end = read_delim_text(text, ',', ifp);
		if (!end) {
			puts(RASTER_EOF);
			break;
		}

		size_t pos = 0;
		do {
			int_fast32_t val;
			pos += read_hex_num(text->buf + pos, &val, align);
			if (val == -1) {
				puts(RASTER_INV);
				return;
			}

			for (int_fast32_t i = 0; i < align * 8; ++i) {
				output[cnt] = ((val >> i) & 1) ? 0x00 : 0xff;
				++cnt;
			}

			if (text->buf[pos] != ',' && cnt < dims) {
				return;
			}
			++pos;
		} while (pos < end && cnt < dims);
	} while (cnt < dims);
}

unsigned char * xbm_decode(const struct xbm_desc *desc) {
	int_fast32_t align;
	switch (desc->type) {
	case xbm_x10: align = 2; break;
	case xbm_x11: align = 1; break;
	default: return NULL;
	}

	struct text_block *text = new_text_block();
	if (!text) {
		return NULL;
	}

	const size_t scanline = (desc->w + 7LU) & ~7LU;
	const size_t dims = scanline * desc->h;
	unsigned char *restrict output = malloc(dims);
	if (!output) {
		free(text);
		return NULL;
	}

	decode_loop(text, desc->ifp, output, dims, align);
	free(text);
	return output;
}

static enum lib_fail read_type(struct xbm_desc *desc) {
	const char bits_and_array[] = "_bits[] = %c";
	const char type_x11[] = "char";
	const char type_x10[] = "short";
	const char unsigned_qual[9] = "unsigned";
	char buf[MAX_NAMELEN + sizeof(bits_and_array)];

	int m = fscanf(desc->ifp, "static %8s ", buf);
	if (m == 1 && !strcmp(unsigned_qual, buf)) {
		m = fscanf(desc->ifp, "%6s ", buf);
	}

	if (m == EOF) {
		return lib_unexpected_eof;
	} else if (m != 1) {
		return lib_unknown_format;
	}

	if (!strcmp(type_x11, buf)) {
		desc->type = xbm_x11;
	} else if (!strcmp(type_x10, buf)) {
		desc->type = xbm_x10;
	} else {
		return lib_invalid_header;
	}

	memcpy(buf, desc->name, desc->name_len);
	memcpy(buf + desc->name_len, bits_and_array, sizeof(bits_and_array));

	char next_char;
	m = fscanf(desc->ifp, buf, &next_char);
	if (m == 1 && next_char == '{') {
		return lib_ok;
	} else if (m == EOF) {
		return lib_unexpected_eof;
	}
	return lib_unknown_format;
}

static enum lib_fail read_xbm_define(char *buf, const size_t tail,
FILE *ifp, const char *str, const size_t str_len, void *val) {
	memcpy(buf + tail, str, str_len);
	char newline;
	int m = fscanf(ifp, buf, val, &newline);
	if (m == 2 && (newline == '\n' || newline == '\r')) {
		return lib_ok;
	} else if (m == EOF) {
		return lib_unexpected_eof;
	}
	return lib_unknown_format;
}

enum lib_fail xbm_read_header(struct xbm_desc *desc) {
	const char height[] = "_height %u%c ";
	const char x_hot[] = "_x_hot %d%c ";
	const char y_hot[] = "_y_hot %d%c ";
	const char define[] = "#define ";
	char scan_buf[MAX_NAMELEN + sizeof(define) + sizeof(height)];

	memcpy(scan_buf, define, sizeof(define));
	memcpy(scan_buf + sizeof(define) - 1, desc->name, desc->name_len);
	const size_t tail = sizeof(define) + desc->name_len - 1 /* NULL */;

	enum lib_fail fail = read_xbm_define(scan_buf, tail, desc->ifp, height,
		sizeof(height), &desc->h);
	if (fail) {
		return fail;
	}

	int next_char = getc(desc->ifp);
	if (next_char == '#') {
		ungetc(next_char, desc->ifp);
		fail = read_xbm_define(scan_buf, tail, desc->ifp, x_hot,
			sizeof(x_hot), &desc->x_hot);
		if (fail) {
			return fail;
		}

		fail = read_xbm_define(scan_buf, tail, desc->ifp, y_hot,
			sizeof(y_hot), &desc->y_hot);
		if (fail) {
			return fail;
		}

		desc->has_hotspot = true;
		next_char = getc(desc->ifp);
	} else {
		desc->has_hotspot = false;
	}

	if (next_char != 's') {
		return lib_unknown_format;
	}
	ungetc(next_char, desc->ifp);
	return read_type(desc);
}

static enum lib_fail get_name(const char *define, char **name,
size_t *name_len) {
	// From "bamboo_width" get "bamboo", and fail on "bamboo%s_width"
	char *name_end = strrchr(define, '_');
	if (!name_end) {
		return lib_unknown_format;
	}
	*name_len = (size_t)(name_end - define);
	if (!*name_len) {
		return lib_unknown_format;
	}

	if (memchr(define, '%', *name_len)) {
		return lib_unknown_format;
	}

	if (strcmp("width", name_end + 1)) {
		return lib_unknown_format;
	}

	*name = malloc(*name_len);
	if (!*name) {
		return lib_alloc_error;
	}
	memcpy(*name, define, *name_len);
	return lib_ok;
}

enum lib_fail xbm_open_file(FILE *ifp, struct xbm_desc *desc) {
	char width_def[MAX_NAMELEN];
	char newline;
	const int res = fscanf(ifp, "#define %63s %u%c ", width_def, &desc->w,
		&newline);
	switch (res) {
	case EOF:
		return lib_unexpected_eof;
	case 3:
		if (newline == '\n' || newline == '\r') {
			break;
		}
		// fallthrough
	default:
		return lib_unknown_format;
	}

	desc->ifp = ifp;
	return get_name(width_def, &desc->name, &desc->name_len);
}
