#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>

#include "../raster/text.h"
#include "../common.h"

#include "xbm.h"

struct xbm_define {
	const struct wustr name;
	unsigned int d;
	bool found;
};

static int toxint_rev(const int digit) {
	switch (digit) {
	case '0': return 0x0;
	case '1': return 0x8;
	case '2': return 0x4;
	case '3': return 0xc;
	case '4': return 0x2;
	case '5': return 0xa;
	case '6': return 0x6;
	case '7': return 0xe;
	case '8': return 0x1;
	case '9': return 0x9;

	case 'A': case 'a': return 0x5;
	case 'B': case 'b': return 0xd;
	case 'C': case 'c': return 0x3;
	case 'D': case 'd': return 0xb;
	case 'E': case 'e': return 0x7;
	case 'F': case 'f': return 0xf;
	}
	return -1;
}

static size_t read_hex_num(const unsigned char *restrict buf, int *val,
const size_t size) {
	size_t i = 0;
	while (isspace(buf[i])) {
		++i;
	}

	/* Pixels in XBM are ordered from least to most significant bit.
	 * We convert the digits directly to their reversed forms and
	 * OR them backwards. */
	if (buf[i] == '0' && (buf[i+1] == 'x' || buf[i+1] == 'X')) {
		i += 2;
		*val = 0;
		for (size_t k = 0; k < size * 2; ++k) {
			int digit = toxint_rev(buf[i]);
			if (digit == -1) {
				break;
			}
			*val |= (digit << (k*4));
			++i;
		}
		return i;
	}
	*val = -1;
	return i;
}

static void convert_loop(const struct xbm_desc *desc, void *restrict output,
const size_t dims, const size_t size) {
	const unsigned char *text = desc->tp.text;
	const size_t end = desc->tp.len;
	size_t pos = desc->tp.pos;
	size_t cnt = 0;
	while (pos < end && cnt < dims) {
		int val;
		pos += read_hex_num(text + pos, &val, size);
		if (val == -1) {
			puts(RASTER_INV);
			break;
		}
		if (size == 2) {
			uint16_t *wout = output;
			wout[cnt] = (uint16_t)val;
		} else {
			uint8_t *out = output;
			out[cnt] = (uint8_t)val;
		}
		++cnt;

		do {
			const unsigned char c = text[pos];
			if (c == ',') {
				break;
			} else if (!isspace(c)) {
				return;
			}
			++pos;
		} while (pos < end);
		++pos;
	}
}

unsigned char * xbm_decode(const struct xbm_desc *desc) {
	const size_t bytes = raster_size(&desc->r);
	void *restrict output = malloc(bytes);
	if (!output) {
		return NULL;
	}

	const size_t size = (desc->type == xbm_x11) ? 1 : 2;
	convert_loop(desc, output, bytes / size, size);
	return output;
}

static bool read_type(struct xbm_desc *desc, struct text_parser *tp,
const struct xbm_define *define) {
	if (!define[0].found || !define[1].found
	|| define[0].d < 1 || define[1].d < 1) {
		return false;
	}

	struct wustr word = text_get_word(tp);
	if (!wustr_eq_str(word, "static")) {
		return false;
	}

	text_skip_space(tp);
	word = text_get_word(tp);
	if (wustr_eq_str(word, "unsigned")) {
		text_skip_space(tp);
		word = text_get_word(tp);
	}

	if (wustr_eq_str(word, "char")) {
		desc->type = xbm_x11;
	} else if (wustr_eq_str(word, "short")) {
		desc->type = xbm_x10;
	} else {
		return false;
	}

	text_skip_space(tp);
	word = text_get_word(tp);
	if (wustr_suffix_str(word, "_bits[]")) {
		int c = text_next_nonspace(tp);
		if (c == '=') {
			c = text_next_nonspace(tp);
			if (c == '{') {
				desc->r = (struct raster_desc) {
					.w = (size_t)define[0].d,
					.h = (size_t)define[1].d,
					.ch = 1,
					.bitdepth = 1,
					.alignment = (desc->type == xbm_x10)
						? 2 : 1,
					.attr = pix_inverted,
				};
				raster_normalize(&desc->r);

				desc->has_hotspot = define[2].found
					&& define[3].found;
				if (desc->has_hotspot) {
					desc->x_hot = define[2].d;
					desc->y_hot = define[3].d;
				}
				return true;
			}
		}
	}
	return false;
}

static bool match_num(struct text_parser *tp, struct xbm_define *define) {
	text_skip_blank(tp);
	const struct wustr word = text_get_word(tp);
	for (size_t i = 0; i < word.len; ++i) {
		const unsigned char c = (unsigned char)word.str[i];
		if (!isdigit(c)) {
			return false;
		}
		const unsigned int d = c - '0';
		if (define->d + d > UINT_MAX/10) {
			return false;
		}
		define->d = define->d*10 + d;
	}
	define->found = true;
	return define->found;
}

static bool parse_define(struct xbm_desc *desc, struct text_parser *tp,
struct xbm_define *define) {
	struct wustr word = text_get_word(tp);
	if (!isblank(text_next_char(tp)) || !wustr_eq_str(word, "define")) {
		return false;
	}

	text_skip_blank(tp);
	word = text_get_word(tp);
	if (!isblank(text_next_char(tp))) {
		return false;
	}

	bool ok = true; // Skip unknown definitions
	for (size_t i = 0; i < 4; ++i) {
		if (wustr_suffix(word, define[i].name)) {
			if (define[i].found) {
				return false;
			}
			if (!desc->name.len) {
				desc->name.str = word.str;
				desc->name.len = word.len - define[i].name.len;
			}
			ok = match_num(tp, define + i);
			break;
		}
	}
	text_skip_line(tp);
	return ok;
}

static const unsigned char *comment_end(const unsigned char *comm,
const unsigned char end, size_t len) {
	const unsigned char *ch = NULL;
	while ( (ch = memchr(comm, end, len)) ) {
		if (ch[0] == '/' && ch[-1] != '*') {
			++ch;
			len -= (size_t)ch - (size_t)comm;
			comm = ch;
			continue;
		}
		break;
	}
	return ch;
}

static bool skip_comment(struct xbm_desc *desc, struct text_parser *tp) {
	unsigned char end;
	switch (text_next_char(tp)) {
	case '*': end = '/'; break;
	case '/': end = '\n'; break;
	default: return false;
	}
	const bool multiline = (end == '/');

	text_skip_space(tp);
	const unsigned char *base = tp->text + tp->pos;
	const unsigned char *comm = comment_end(base, end, tp->len - tp->pos);
	if (comm) {
		size_t len = (size_t)comm - (size_t)base;
		tp->pos += len;

		if (multiline) {
			text_skip_line(tp);
			--len;
		}
		if (!desc->comment.len) {
			while (len && isspace(base[len - 1])) {
				--len;
			}
			desc->comment.str = base;
			desc->comment.len = len;
		}
		return true;
	}
	return false;
}

enum lib_fail xbm_open_mem(struct xbm_desc *desc, const struct mmap_info *mem) {
	struct text_parser *tp = &desc->tp;
	text_parser_mem(tp, mem->len, mem->data);

	desc->comment.len = 0;
	desc->name.len = 0;

	struct xbm_define define[] = {
		{.name = wustr_str("_width")},
		{.name = wustr_str("_height")},
		{.name = wustr_str("_x_hot")},
		{.name = wustr_str("_y_hot")},
	};

	bool ok = false;
	do {
		const int c = text_next_nonspace(tp);
		if (c == '/') {
			ok = skip_comment(desc, tp);
		} else if (c == '#') {
			ok = parse_define(desc, tp, define);
		} else if (c == 's') { // static
			--tp->pos;
			ok = read_type(desc, tp, define);
			break;
		} else {
			ok = false;
		}
	} while (ok && tp->pos < tp->len);

	if (ok) {
		return lib_ok;
	}
	return (tp->pos >= tp->len)
		? lib_unexpected_eof : lib_invalid_header;
}
