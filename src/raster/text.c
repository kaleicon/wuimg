#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "text.h"

static int toxnum(const int c) {
	switch (c) {
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

static bool fill_buf(struct text_block *text, FILE *ifp, size_t *read) {
	const size_t rem = sizeof(text->buf) - text->tail;
	memcpy(text->buf, text->buf + rem, text->tail);
	*read = fread(text->buf + text->tail, 1, rem, ifp);
	return rem == *read;
}

size_t text_block_read_spaced(struct text_block *text, FILE *ifp) {
	size_t read;
	if (fill_buf(text, ifp, &read)) {
		size_t end = sizeof(text->buf);
		while (end && isgraph(text->buf[end - 1])) {
			--end;
		}
		text->tail = sizeof(text->buf) - end;
		while (end && isspace(text->buf[end - 1])) {
			--end;
		}
		return end;
	} else {
		text->buf[text->tail + read] = 0;
		text->tail = 0;
	}
	return read;
}

struct text_block * text_block_new(void) {
	struct text_block *b = malloc(sizeof(*b));
	if (b) {
		b->tail = 0;
		b->buf[sizeof(b->buf) - 1] = 0;
	}
	return b;
}

size_t text_read_uint(const unsigned char *restrict buf, text_fast_t *val,
const size_t digits) {
	size_t i = 0;
	while (isspace(buf[i])) {
		++i;
	}

	*val = 0;
	for (size_t k = 0; k < digits && isdigit(buf[i]); ++k, ++i) {
		*val = *val * 10 + buf[i] - '0';
	}
	return i;
}


static bool bndchk(struct text_parser *tp) {
	return tp->pos < tp->len;
}

static unsigned char curc(struct text_parser *tp) {
	return tp->text[tp->pos];
}


unsigned char text_next_char_unsafe(struct text_parser *tp) {
	const unsigned char c = curc(tp);
	++tp->pos;
	return c;
}

size_t text_get_uint_unsafe(struct text_parser *tp, size_t digits,
text_fast_t *val) {
	*val = 0;
	size_t k = 0;
	while (k < digits && isdigit(curc(tp))) {
		*val = *val * 10 - '0' + curc(tp);
		++tp->pos;
		++k;
	}
	return k;
}

void text_skip_blank(struct text_parser *tp) {
	while (bndchk(tp) && isblank(curc(tp))) {
		++tp->pos;
	}
}

void text_skip_space(struct text_parser *tp) {
	while (bndchk(tp) && isspace(curc(tp))) {
		++tp->pos;
	}
}

void text_skip_nonspace(struct text_parser *tp) {
	while (bndchk(tp) && !isspace(curc(tp))) {
		++tp->pos;
	}
}

void text_skip_line(struct text_parser *tp) {
	const unsigned char *newline = memchr(tp->text + tp->pos, '\n',
		tp->len - tp->pos);
	if (newline) {
		tp->pos = (size_t)newline - (size_t)tp->text;
	} else {
		tp->pos = tp->len;
	}
}

int text_next_char(struct text_parser *tp) {
	if (bndchk(tp)) {
		return text_next_char_unsafe(tp);
	}
	return EOF;
}

int text_next_nonblank(struct text_parser *tp) {
	text_skip_blank(tp);
	return text_next_char(tp);
}

int text_next_nonspace(struct text_parser *tp) {
	text_skip_space(tp);
	return text_next_char(tp);
}

struct wuptr text_get_word(struct text_parser *tp) {
	const size_t start = tp->pos;
	text_skip_nonspace(tp);
	return wuptr_mem(tp->text + start, tp->pos - start);
}

size_t text_get_uint(struct text_parser *tp, size_t digits, text_fast_t *val) {
	const size_t rem = tp->len - tp->pos;
	if (rem < digits) {
		digits = rem;
	}
	return text_get_uint_unsafe(tp, digits, val);
}

static bool scan_end(struct text_parser *tp, const size_t start,
const size_t size, const bool read, const uint_fast64_t num, void *val) {
	uint_fast64_t max;
	switch (size) {
	case 1: max = UINT8_MAX; break;
	case 2: max = UINT16_MAX; break;
	case 4: max = UINT32_MAX; break;
	case 8: max = UINT64_MAX; break;
	default:
		tp->pos = start;
		return false;
	}

	if (read && num <= max) {
		switch (size) {
		case 1: *(uint8_t *)val = (uint8_t)num; break;
		case 2: *(uint16_t *)val = (uint16_t)num; break;
		case 4: *(uint32_t *)val = (uint32_t)num; break;
		case 8: *(uint64_t *)val = (uint64_t)num; break;
		}
		return true;
	}
	tp->pos = start;
	return false;
}

bool text_scan_xint(struct text_parser *tp, const size_t size, void *val) {
	const size_t start = tp->pos;
	if (text_next_char(tp) != '0' || toupper(text_next_char(tp)) != 'X') {
		tp->pos = start;
		return false;
	}

	bool read = false;
	uint_fast64_t num = 0;
	while (bndchk(tp) && isxdigit(curc(tp))) {
		const uint_fast64_t before = num;
		num = num * 16 + (uint_fast64_t)toxnum(text_next_char_unsafe(tp));
		if (num < before) {
			tp->pos = start;
			return false;
		}
		read = true;
	}
	return scan_end(tp, start, size, read, num, val);
}

bool text_scan_uint(struct text_parser *tp, const size_t size, void *val) {
	const size_t start = tp->pos;
	bool read = false;
	uint_fast64_t num = 0;
	while (bndchk(tp) && isdigit(curc(tp))) {
		const uint_fast64_t before = num;
		num = num * 10 - '0' + text_next_char_unsafe(tp);
		if (num < before) {
			tp->pos = start;
			return false;
		}
		read = true;
	}
	return scan_end(tp, start, size, read, num, val);
}

struct text_parser text_parser_mem(const size_t len, const void *text) {
	return (struct text_parser) {
		.len = len,
		.text = text,
	};
}
