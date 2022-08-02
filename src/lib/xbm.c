#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>

#include "xbm.h"

struct xbm_define {
	const struct wuptr name;
	size_t d;
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

static size_t read_rev_hex_num(const unsigned char *restrict buf, int *val,
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

size_t xbm_decode(const struct xbm_desc *desc, struct raw_img *img) {
	size_t cnt = 0;
	if (raw_img_alloc_noverify(img)) {
		const size_t size = desc->type;
		const size_t dims = raw_img_size(img) / size;

		const unsigned char *text = desc->tp.mem;
		const size_t end = desc->tp.len;
		size_t pos = desc->tp.pos;
		while (pos < end && cnt < dims) {
			int val;
			pos += read_rev_hex_num(text + pos, &val, size);
			if (val == -1) {
				break;
			}
			if (desc->type == xbm_x10) {
				uint16_t *wout = (uint16_t *)img->data;
				wout[cnt] = (uint16_t)val;
			} else {
				uint8_t *out = img->data;
				out[cnt] = (uint8_t)val;
			}
			++cnt;

			do {
				const unsigned char c = text[pos];
				if (c == ',') {
					break;
				} else if (!isspace(c)) {
					return cnt;
				}
				++pos;
			} while (pos < end);
			++pos;
		}
	}
	return cnt;
}

static bool read_type(struct xbm_desc *desc, struct raw_img *img,
struct mp_parser *tp, const struct xbm_define *define) {
	if (!define[0].found || !define[1].found
	|| define[0].d < 1 || define[1].d < 1) {
		return false;
	}

	struct wuptr word = mp_get_word(tp);
	if (!wuptr_eq_str(word, "static")) {
		return false;
	}

	mp_skip_space(tp);
	word = mp_get_word(tp);
	if (wuptr_eq_str(word, "unsigned")) {
		mp_skip_space(tp);
		word = mp_get_word(tp);
	}

	if (wuptr_eq_str(word, "char")) {
		desc->type = xbm_x11;
	} else if (wuptr_eq_str(word, "short")) {
		desc->type = xbm_x10;
	} else {
		return false;
	}

	mp_skip_space(tp);
	word = mp_get_word(tp);
	if (wuptr_suffix_str(word, "_bits[]")) {
		int c = mp_next_nonspace(tp);
		if (c == '=') {
			c = mp_next_nonspace(tp);
			if (c == '{') {
				img->w = (size_t)define[0].d;
				img->h = (size_t)define[1].d;
				img->channels = 1;
				img->bitdepth = 1;
				img->alignment = (desc->type == xbm_x10) ? 2 : 1;
				img->attr = pix_inverted;

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

static bool match_num(struct mp_parser *tp, struct xbm_define *define) {
	mp_skip_blank(tp);
	const struct wuptr word = mp_get_word(tp);
	for (size_t i = 0; i < word.len; ++i) {
		const unsigned char c = (unsigned char)word.ptr[i];
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

static bool parse_define(struct xbm_desc *desc, struct mp_parser *tp,
struct xbm_define *define) {
	struct wuptr word = mp_get_word(tp);
	if (!isblank(mp_next_char(tp)) || !wuptr_eq_str(word, "define")) {
		return false;
	}

	mp_skip_blank(tp);
	word = mp_get_word(tp);
	if (!isblank(mp_next_char(tp))) {
		return false;
	}

	bool ok = true; // Skip unknown definitions
	for (size_t i = 0; i < 4; ++i) {
		if (wuptr_suffix(word, define[i].name)) {
			if (define[i].found) {
				return false;
			}
			if (!desc->name.len) {
				desc->name.ptr = word.ptr;
				desc->name.len = word.len - define[i].name.len;
			}
			ok = match_num(tp, define + i);
			break;
		}
	}
	mp_skip_line(tp);
	return ok;
}

static const unsigned char * comment_end(const unsigned char *comm,
const unsigned char end, size_t len) {
	const unsigned char *ch;
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

static bool skip_comment(struct xbm_desc *desc, struct mp_parser *tp) {
	const unsigned char *base = tp->mem + tp->pos - 1;
	unsigned char end;
	switch (mp_next_char(tp)) {
	case '*': end = '/'; break;
	case '/': end = '\n'; break;
	default: return false;
	}
	const unsigned char *comm = comment_end(tp->mem + tp->pos, end,
		tp->len - tp->pos);
	if (comm) {
		size_t len = (size_t)comm - (size_t)base - (end == '\n');
		tp->pos += len;
		if (!desc->comment.len) {
			desc->comment.ptr = base;
			desc->comment.len = len;
		}
		return true;
	}
	return false;
}

enum wu_error xbm_parse_header(struct xbm_desc *desc, struct raw_img *img,
const struct map_info *mm) {
	struct mp_parser *tp = &desc->tp;
	*tp = mp_parser_mem(mm->len, mm->data);

	desc->comment.len = 0;
	desc->name.len = 0;

	struct xbm_define define[] = {
		{.name = wuptr_str("_width")},
		{.name = wuptr_str("_height")},
		{.name = wuptr_str("_x_hot")},
		{.name = wuptr_str("_y_hot")},
	};

	bool ok = false;
	do {
		const int c = mp_next_nonspace(tp);
		if (c == '/') {
			ok = skip_comment(desc, tp);
		} else if (c == '#') {
			ok = parse_define(desc, tp, define);
		} else if (c == 's') { // static
			--tp->pos;
			ok = read_type(desc, img, tp, define);
			if (ok) {
				return raw_img_verify(img);
			}
			break;
		} else {
			ok = false;
		}
	} while (ok && tp->pos < tp->len);
	return (tp->pos >= tp->len)
		? wu_unexpected_eof : wu_invalid_header;
}
