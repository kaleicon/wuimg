// SPDX-License-Identifier: 0BSD
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "misc/math.h"
#include "misc/memparser.h"

static long tonum(const long c) {
	return c - '0';
}

static long toxnum(const long c) {
	switch (c) {
	case 'A': case 'a': return 0xa;
	case 'B': case 'b': return 0xb;
	case 'C': case 'c': return 0xc;
	case 'D': case 'd': return 0xd;
	case 'E': case 'e': return 0xe;
	case 'F': case 'f': return 0xf;
	}
	return tonum(c);
}

static bool bndchk(const struct mp_parser *mp) {
	return mp->pos < mp->len;
}

static unsigned char curc(const struct mp_parser *mp) {
	return mp->mem[mp->pos];
}


size_t mp_skip_space_unsafe(struct mp_parser *mp) {
	size_t k = 0;
	while (isspace(curc(mp))) {
		++mp->pos;
		++k;
	}
	return k;
}

unsigned char mp_next_char_unsafe(struct mp_parser *mp) {
	const unsigned char c = curc(mp);
	++mp->pos;
	return c;
}

size_t mp_get_uint_unsafe(struct mp_parser *mp, long *val) {
	*val = 0;
	const size_t start = mp->pos;
	while (isdigit(curc(mp))) {
		*val = *val * 10 + tonum(mp_next_char_unsafe(mp));
	}
	return mp->pos - start;
}


void mp_skip_blank(struct mp_parser *mp) {
	while (bndchk(mp) && isblank(curc(mp))) {
		++mp->pos;
	}
}

size_t mp_skip_space(struct mp_parser *mp) {
	size_t k = 0;
	while (bndchk(mp) && isspace(curc(mp))) {
		++mp->pos;
		++k;
	}
	return k;
}

void mp_skip_nonspace(struct mp_parser *mp) {
	while (bndchk(mp) && !isspace(curc(mp))) {
		++mp->pos;
	}
}

void mp_skip_line(struct mp_parser *mp) {
	const unsigned char *loc = memchr(mp->mem + mp->pos, '\n',
		mp->len - mp->pos);
	if (loc) {
		mp->pos = (size_t)loc - (size_t)mp->mem;
	} else {
		mp->pos = mp->len;
	}
}

int mp_next_char(struct mp_parser *mp) {
	if (bndchk(mp)) {
		return mp_next_char_unsafe(mp);
	}
	return EOF;
}

int mp_next_nonblank(struct mp_parser *mp) {
	mp_skip_blank(mp);
	return mp_next_char(mp);
}

int mp_next_nonspace(struct mp_parser *mp) {
	mp_skip_space(mp);
	return mp_next_char(mp);
}

struct wuptr mp_next_remaining(struct mp_parser *mp, const size_t len) {
	const size_t rem = zumin(mp->len - mp->pos, len);
	const struct wuptr wp = wuptr_mem(mp->mem + mp->pos, rem);
	mp->pos += rem;
	return wp;
}

const uint8_t * mp_next_slice(struct mp_parser *mp, const size_t len) {
	if (mp->len - mp->pos >= len) {
		const uint8_t *slice = mp->mem + mp->pos;
		mp->pos += len;
		return slice;
	}
	return NULL;
}

struct wuptr mp_get_word(struct mp_parser *mp) {
	const size_t start = mp->pos;
	mp_skip_nonspace(mp);
	return wuptr_mem(mp->mem + start, mp->pos - start);
}

size_t mp_get_uint(struct mp_parser *mp, size_t digits, long *val) {
	digits = zumin(digits, mp->len - mp->pos);
	*val = 0;
	size_t k = 0;
	while (k < digits) {
		const uint8_t c = curc(mp);
		if (!isdigit(c)) {
			break;
		}
		*val = *val * 10 + tonum(c);
		++k;
		++mp->pos;
	}
	return k;
}

size_t mp_get_int(struct mp_parser *mp, size_t digits, long *val) {
	bool sign = false;
	if (curc(mp) == '-') {
		++mp->pos;
		sign = true;
	}
	const size_t k = mp_get_uint(mp, digits, val);
	if (sign) {
		*val = -*val;
	}
	return k;
}

size_t mp_get_xint(struct mp_parser *mp, size_t digits, long *val) {
	const uint8_t *pre = mp_next_slice(mp, 2);
	bool hex = false;
	if (pre) {
		if (pre[0] == '0' && (pre[1] == 'x' || pre[1] == 'X')) {
			hex = true;
		} else {
			mp->pos -= 2;
		}
	}
	if (hex) {
		digits = zumin(digits, mp->len - mp->pos);
		*val = 0;
		size_t k = 0;
		while (k < digits) {
			const uint8_t c = curc(mp);
			if (!isxdigit(c)) {
				break;
			}
			*val = *val * 16 + toxnum(c);
			++k;
			++mp->pos;
		}
		return k;
	}
	return mp_get_uint(mp, digits, val);
}

struct wuptr mp_remaining_at(const struct mp_parser *mp, const size_t pos,
const size_t len) {
	return (struct wuptr) {
		.ptr = mp->mem + pos,
		.len = (pos <= mp->len) ? zumin(mp->len - mp->pos, len) : 0,
	};
}

const uint8_t * mp_slice_at(const struct mp_parser *mp, const size_t pos,
const size_t len) {
	if (pos <= mp->len && mp->len - pos >= len) {
		return mp->mem + pos;
	}
	return NULL;
}

struct mp_parser mp_parser_mem(const size_t len, const void *mem) {
	return (struct mp_parser) {
		.len = len,
		.mem = mem,
	};
}

struct mp_parser mp_parser_map(const struct map_info mm) {
	return mp_parser_mem(mm.len, mm.data);
}
