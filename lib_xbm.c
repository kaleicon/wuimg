#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "common.h"
#include "common_unpack.h"

#include "lib_xbm.h"

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
		const size_t end = read_spaced_text(text, ifp);
		if (!end) {
			puts("XBM Error: Unexpected End of File.");
			break;
		}

		size_t pos = 0;
		do {
			int_fast32_t val;
			pos += read_hex_num(text->buf + pos, &val, align);
			if (val == -1) {
				puts("XBM Error: Invalid data found while decoding");
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

static enum lib_fail check_name(char *buf, struct xbm_desc *desc) {
	const size_t len = desc->name_len + 1;
	const size_t read = fread(buf, 1, len, desc->ifp);
	if (read != len) {
		return lib_unexpected_eof;
	} else if (memcmp(buf, desc->name, desc->name_len)
	|| buf[desc->name_len] != '_') {
		return lib_unknown_format;
	}
	return lib_ok;
}

static enum lib_fail read_type(char *buf, struct xbm_desc *desc) {
	const char type_x11[] = "char";
	const char type_x10[6] = "short";
	const char unsigned_qual[9] = "unsigned";
	char file_type[9];
	int m = fscanf(desc->ifp, "static %8s ", file_type);
	if (m == 1 && !strcmp(unsigned_qual, file_type)) {
		m = fscanf(desc->ifp, "%6s ", file_type);
	}

	if (m == EOF) {
		return lib_unexpected_eof;
	} else if (m != 1) {
		return lib_unknown_format;
	}

	if (!strcmp(type_x11, file_type)) {
		desc->type = xbm_x11;
	} else if (!strcmp(type_x10, file_type)) {
		desc->type = xbm_x10;
	} else {
		return lib_invalid_header;
	}

	enum lib_fail fail = check_name(buf, desc);
	if (fail) {
		return fail;
	}

	char next_char;
	m = fscanf(desc->ifp, "bits[] = {%c", &next_char);
	if (m == EOF) {
		return lib_unexpected_eof;
	} else if (m != 1) {
		return lib_unknown_format;
	}
	ungetc(next_char, desc->ifp);
	return lib_ok;
}

static enum lib_fail read_xbm_line(char *buf, struct xbm_desc *desc,
const char *str, void *val) {
	char space;
	int m = fscanf(desc->ifp, " #define%c ", &space);
	if (m == EOF) {
		return lib_unexpected_eof;
	} else if (m != 1 || !isspace(space)) {
		return lib_unknown_format;
	}

	enum lib_fail fail = check_name(buf, desc);
	if (fail) {
		return fail;
	}

	m = fscanf(desc->ifp, str, val);
	if (m == EOF) {
		return lib_unexpected_eof;
	} else if (m != 1) {
		return lib_unknown_format;
	}
	return lib_ok;
}

static enum lib_fail read_defines(char *buf, struct xbm_desc *desc) {
	enum lib_fail fail = read_xbm_line(buf, desc, "height %u", &desc->h);
	if (fail) {
		return fail;
	}

	char next_char;
	int m = fscanf(desc->ifp, " %c", &next_char);
	if (m == EOF) {
		return lib_unexpected_eof;
	} else if (m != 1) {
		return lib_unknown_format;
	}

	if (next_char == '#') {
		ungetc(next_char, desc->ifp);
		fail = read_xbm_line(buf, desc, "x_hot %d", &desc->x_hot);
		if (fail) {
			return fail;
		}
		fail = read_xbm_line(buf, desc, "y_hot %d", &desc->y_hot);
		if (fail) {
			return fail;
		}

		m = fscanf(desc->ifp, " %c", &next_char);
		if (m == EOF) {
			return lib_unexpected_eof;
		} else if (m != 1) {
			return lib_unknown_format;
		}
		desc->has_hotspot = true;
	} else {
		desc->has_hotspot = false;
	}

	if (next_char == 's') {
		ungetc(next_char, desc->ifp);
	}
	return read_type(buf, desc);
}


enum lib_fail xbm_read_header(struct xbm_desc *desc) {
	char *buf = malloc(desc->name_len + 1);

	enum lib_fail fail = read_defines(buf, desc);
	free(buf);
	return fail;
}

static enum lib_fail get_name(char *width_def, size_t *name_len) {
	char *w_end = strrchr(width_def, '_');
	if (!w_end) {
		return lib_unknown_format;
	}
	w_end[0] = 0;
	*name_len = (size_t)(w_end - width_def);

	if (strcmp("width", w_end + 1)) {
		return lib_unknown_format;
	}
	return lib_ok;
}

enum lib_fail xbm_open_file(FILE *ifp, struct xbm_desc *desc) {
	char width_def[32];
	char newline;
	const int res = fscanf(ifp, "#define %31s %u%c", width_def, &desc->w,
		&newline);
	switch (res) {
	case EOF:
		return lib_unexpected_eof;
	case 3:
		if (newline == '\n') {
			break;
		}
		// Fallthrough
	default:
		return lib_unknown_format;
	}

	size_t name_len;
	enum lib_fail status = get_name(width_def, &name_len);
	if (status == lib_ok) {
		if (name_len == 0) {
			status = lib_unknown_format;
		} else {
			desc->name = strdup(width_def);
			desc->name_len = name_len;
			desc->ifp = ifp;
		}
	}
	return status;
}
