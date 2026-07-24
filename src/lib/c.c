// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "lib/c.h"

struct wu_st c_decode(const struct c_desc *desc, struct wuimg *img) {
	const uint8_t size = desc->fmt == c_xbm && desc->xbm.version == xbm_x11
		? 1 : 2;
	const size_t dims = wuimg_size(img);

	size_t cnt = 0;
	struct mparser tp = desc->tp;
	while (cnt < dims) {
		mp_skip_space(&tp);
		if (tp.len - tp.pos < 2u + size*2) {
			break;
		}

		uintmax_t val;
		mp_scan_xint(&tp, 4, &val);
		if (size == 1) {
			img->data[cnt] = (uint8_t)val;
		} else {
			img->data[cnt] = (uint8_t)(val >> 8);
			img->data[cnt+1] = (uint8_t)val;
		}
		cnt += size;

		if (mp_next_char(&tp) != ',') {
			break;
		}
	}
	return wuerr_partial(cnt, dims);
}

enum c_tok_type {
	c_comment,
	c_define,
	c_identifier,
	c_number,
	c_delimiter,
};

struct c_tok {
	enum c_tok_type type;
	struct wuptr str;
};

static struct wu_st comment(struct mparser *tp, struct c_tok *tok) {
	const size_t start = tp->pos;
	tok->str.ptr = tp->mem + start;
	while (mp_skip_until(tp, '/')) {
		if (tp->mem[tp->pos-2] == '*') {
			tok->str.len = tp->pos - 2 - start;
			return WU_OK;
		}
	}
	return WUERR_HERE(wu_unexpected_eof);
}

static struct wu_st word(struct mparser *tp, struct c_tok *tok) {
	tok->str.ptr = tp->mem + tp->pos;
	tok->str.len = 0;
	for (;;) {
		const int c = mp_cur_char(tp);
		if (c < 0x80 && !isalnum(c) && c != '_') {
			break;
		}
		mp_next_char(tp);
		++tok->str.len;
	}
	return WU_OK;
}

static struct wu_st num_lit(struct mparser *tp, struct c_tok *tok) {
	tok->str.ptr = tp->mem + tp->pos;
	tok->str.len = 0;
	int c = mp_cur_char(tp);
	if (c == '-') {
		mp_next_char(tp);
		++tok->str.len;
	}
	for (;;) {
		c = mp_cur_char(tp);
		if (!isalnum(c) && c != '.') {
			break;
		}
		mp_next_char(tp);
		++tok->str.len;
	}
	return WU_OK;
}

static struct wu_st next_tok(struct mparser *tp, struct c_tok *tok) {
	mp_skip_space(tp);
	const int c = mp_cur_char(tp);
	switch (c) {
	case EOF: return WUERR_HERE(wu_unexpected_eof);
	case '/':
		mp_next_char(tp);
		if (mp_next_char(tp) == '*') {
			tok->type = c_comment;
			return comment(tp, tok);
		}
		tp->pos -= 2;
		break;
	case '*':
		mp_next_char(tp);
		if (mp_next_char(tp) == '/') {
			return wuerr(wu_invalid_header,
				"stray comment end (*/)");
		}
		tp->pos -= 2;
		break;
	case '#':
		if (tp->pos != 0 && tp->mem[tp->pos-1] != '\n') {
			return wuerr(wu_invalid_header,
				"preprocessor directive not at beginning of line");
		}
		mp_next_char(tp);
		word(tp, tok);
		tok->type = c_define;
		if (wuptr_eq_str(tok->str, "define") && isblank(mp_cur_char(tp))) {
			return WU_OK;
		}
		return wuerr(wu_invalid_header,
			"expected #define directive");
	case '-':
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		tok->type = c_number;
		num_lit(tp, tok);
		return WU_OK;
	default:
		if (isalpha(c)) {
			tok->type = c_identifier;
			return word(tp, tok);
		}
	}
	tok->type = c_delimiter;
	tok->str = mp_avail(tp, 1);
	return WU_OK;
}

static bool tok_eq(const struct c_tok *tok, enum c_tok_type type,
const char *str) {
	return str
		? wuptr_eq_str(tok->str, str)
		: tok->type == type;
}

static struct wu_st expect_tok(struct c_desc *desc, struct c_tok *tok,
const enum c_tok_type type, const char *str) {
	struct wu_st st = next_tok(&desc->tp, tok);
	if (wu_isok(st) && !tok_eq(tok, type, str)) {
		return wuerr(wu_invalid_header, "unexpected token");
	}
	return st;
}

static struct wu_st expect_toks(struct c_desc *desc, struct c_tok *tok,
const char **str, const size_t len) {
	struct wu_st st;
	for (size_t i = 0; i < len; ++i) {
		st = expect_tok(desc, tok, 0, str[i]);
		if (!wu_isok(st)) {
			break;
		}
	}
	return st;
}

static struct wu_st array_contents(struct c_desc *desc, struct wuimg *img) {
	struct c_tok tok;
	const char *strs[] = {"]", "=", "{"};
	struct wu_st st = expect_toks(desc, &tok, strs, ARRAY_LEN(strs));
	if (!wu_isok(st)) {
		return st;
	}

	size_t pos = desc->tp.pos;
	while (wu_isok((st = next_tok(&desc->tp, &tok)))
	&& tok_eq(&tok, c_comment, NULL)) {
		pos = desc->tp.pos;
	}
	if (!wu_isok(st)) {
		return st;
	}
	desc->tp.pos = pos;
	img->channels = 1;
	img->bitdepth = 1;
	img->cs.invert = true;
	return wuerr(wuimg_verify(img), NULL);
}

static struct wu_st xbm_array_def(struct c_desc *desc, struct wuimg *img,
struct c_tok tok) {
	if (!wuptr_eq_str(tok.str, "static")) {
		return wuerr(wu_invalid_header, "expected \"static\" qualifier");
	}
	struct wu_st st = next_tok(&desc->tp, &tok);
	if (!wu_isok(st)) {
		return st;
	} else if (wuptr_eq_str(tok.str, "unsigned")) {
		st = next_tok(&desc->tp, &tok);
		if (!wu_isok(st)) {
			return st;
		}
	}

	if (wuptr_eq_str(tok.str, "char")) {
		desc->xbm.version = xbm_x11;
		img->align_sh = 0;
	} else if (wuptr_eq_str(tok.str, "short")) {
		desc->xbm.version = xbm_x10;
		img->align_sh = 1;
	} else {
		return wuerr(wu_invalid_header, "bad array type (XBM)");
	}
	st = next_tok(&desc->tp, &tok);
	if (!wu_isok(st)) {
		return st;
	} else if (!wuptr_suffix_str(tok.str, "_bits")) {
		return wuerr(wu_invalid_header, "unknown array name suffix");
	}
	st = expect_tok(desc, &tok, c_delimiter, "[");
	return wu_isok(st) ? array_contents(desc, img) : st;
}

static struct wu_st degas_array_def(struct c_desc *desc, struct wuimg *img,
struct c_tok tok) {
	img->align_sh = 1;
	if (!wuptr_eq_str(tok.str, "int")) {
		return wuerr(wu_invalid_header, "bad array type (degas icon)");
	}
	const char *strs[] = {"image", "[", "ICONSIZE"};
	struct wu_st st = expect_toks(desc, &tok, strs, ARRAY_LEN(strs));
	return wu_isok(st) ? array_contents(desc, img) : st;
}

static struct wu_st identify_from_comment(struct c_desc *desc,
const struct c_tok *tok) {
	if (wuptr_eq_str(tok->str, " DEGAS Elite Icon Definition ")) {
		desc->fmt = c_degas_icon;
		return WU_OK;
	}
	desc->fmt = c_xbm;
	return WU_OK;
}

static struct wu_st define_value(struct c_desc *desc, struct wuimg *img,
const struct wuptr name, const struct wuptr val) {
	struct mparser mp = mp_wuptr(val);
	switch (desc->fmt) {
	case c_xbm:
		;intmax_t i;
		mp_scan_int(&mp, SIZE_MAX, &i);
		if (wuptr_suffix_str(name, "_width")) {
			img->w = (size_t)i;
		} else if (wuptr_suffix_str(name, "_height")) {
			img->h = (size_t)i;
		} else if (wuptr_suffix_str(name, "_x_hot")) {
			desc->xbm.x_hot = i;
			desc->xbm.has_hotspot = true;
		} else if (wuptr_suffix_str(name, "_y_hot")) {
			desc->xbm.y_hot = i;
			desc->xbm.has_hotspot = true;
		}
		break;
	case c_degas_icon:
		;uintmax_t x;
		mp_scan_xint(&mp, SIZE_MAX, &x);
		if (wuptr_eq_str(name, "ICON_W")) {
			img->w = (size_t)x;
		} else if (wuptr_eq_str(name, "ICON_H")) {
			img->h = (size_t)x;
		} else if (!wuptr_eq_str(name, "ICONSIZE")) {
			return wuerr(wu_invalid_header, "unknown macro variable");
		}
		break;
	}
	return WU_OK;
}

static struct wu_st parse_define(struct c_desc *desc, struct wuimg *img) {
	struct c_tok name, val;
	struct wu_st st = expect_tok(desc, &name, c_identifier, NULL);
	if (!wu_isok(st)) {
		return st;
	}
	st = expect_tok(desc, &val, c_number, NULL);
	if (!wu_isok(st)) {
		return st;
	}
	return define_value(desc, img, name.str, val.str);
}

struct wu_st c_parse(struct c_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	*desc = (struct c_desc) {.tp = mp_wuptr(mem), .fmt = c_xbm};
	struct c_tok tok;
	struct wu_st st;
	for (bool first = true;; first = false) {
		st = next_tok(&desc->tp, &tok);
		if (!wu_isok(st)) {
			break;
		}

		switch (tok.type) {
		case c_comment:
			if (first) {
				st = identify_from_comment(desc, &tok);
				if (!wu_isok(st)) {
					return st;
				}
			}
			break;
		case c_define:
			st = parse_define(desc, img);
			if (!wu_isok(st)) {
				return st;
			}
			break;
		case c_identifier:
			if (desc->fmt == c_xbm) {
				img->bit = little_endian;
				return xbm_array_def(desc, img, tok);
			}
			return degas_array_def(desc, img, tok);
		default:
			return wuerr(wu_invalid_header,
				"unexpected token while parsing definitions");
		}
	}
	return st;
}
