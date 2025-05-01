// SPDX-License-Identifier: 0BSD
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "misc/math.h"
#include "misc/mparser.h"

static uint8_t tonum(const uint8_t c) {
	return c - '0';
}

static uint8_t toxnum(const uint8_t c) {
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

static bool bndchk(const struct mparser *mp) {
	return mp->pos < mp->len;
}

static unsigned char curc(const struct mparser *mp) {
	return mp->mem[mp->pos];
}

static void mp_skip_nonspace(struct mparser *mp) {
	while (bndchk(mp) && !isspace(curc(mp))) {
		++mp->pos;
	}
}


size_t mp_skip_space_unsafe(struct mparser *mp) {
	size_t k = 0;
	while (isspace(curc(mp))) {
		++mp->pos;
		++k;
	}
	return k;
}

unsigned char mp_next_char_unsafe(struct mparser *mp) {
	const unsigned char c = curc(mp);
	++mp->pos;
	return c;
}

size_t mp_scan_uint_unsafe(struct mparser *mp, uintmax_t *val) {
	*val = 0;
	const size_t start = mp->pos;
	while (isdigit(curc(mp))) {
		*val = *val * 10 + tonum(mp_next_char_unsafe(mp));
	}
	return mp->pos - start;
}


void mp_skip_blank(struct mparser *mp) {
	while (bndchk(mp) && isblank(curc(mp))) {
		++mp->pos;
	}
}

size_t mp_skip_space(struct mparser *mp) {
	size_t k = 0;
	while (bndchk(mp) && isspace(curc(mp))) {
		++mp->pos;
		++k;
	}
	return k;
}

bool mp_skip_until(struct mparser *mp, int c) {
	const uint8_t *loc = memchr(mp->mem + mp->pos, c, mp->len - mp->pos);
	if (loc) {
		mp->pos = (size_t)loc - (size_t)mp->mem + 1;
	} else {
		mp->pos = mp->len;
	}
	return loc;
}

int mp_cur_char(struct mparser *mp) {
	return bndchk(mp) ? curc(mp) : EOF;
}

int mp_next_char(struct mparser *mp) {
	if (bndchk(mp)) {
		return mp_next_char_unsafe(mp);
	}
	return EOF;
}

int mp_next_nonblank(struct mparser *mp) {
	mp_skip_blank(mp);
	return mp_next_char(mp);
}

int mp_next_nonspace(struct mparser *mp) {
	mp_skip_space(mp);
	return mp_next_char(mp);
}

struct wuptr mp_next_word(struct mparser *mp) {
	const size_t start = mp->pos;
	mp_skip_nonspace(mp);
	return wuptr_mem(mp->mem + start, mp->pos - start);
}


const uint8_t * mp_slice_at(const struct mparser *mp, const size_t pos,
const size_t len) {
	if (pos <= mp->len && mp->len - pos >= len) {
		return mp->mem + pos;
	}
	return NULL;
}

struct wuptr mp_avail_at(const struct mparser *mp, const size_t pos,
const size_t len) {
	return (struct wuptr) {
		.ptr = mp->mem + pos,
		.len = (pos <= mp->len) ? zumin(mp->len - mp->pos, len) : 0,
	};
}

const uint8_t * mp_slice(struct mparser *mp, const size_t len) {
	if (mp->len - mp->pos >= len) {
		const uint8_t *slice = mp->mem + mp->pos;
		mp->pos += len;
		return slice;
	}
	return NULL;
}

struct wuptr mp_avail(struct mparser *mp, const size_t len) {
	const size_t rem = zumin(mp->len - mp->pos, len);
	const struct wuptr wp = wuptr_mem(mp->mem + mp->pos, rem);
	mp->pos += rem;
	return wp;
}

struct wuptr mp_remaining(struct mparser *mp) {
	return mp_avail(mp, mp->len - mp->pos);
}

bool mp_upto(struct mparser *mp, struct wuptr *out, const char chr) {
	out->ptr = mp->mem + mp->pos;
	const size_t rem = mp->len - mp->pos;
	const uint8_t *end = memchr(out->ptr, chr, rem);
	if (end) {
		out->len = (size_t)(end - out->ptr);
		mp->pos = (size_t)(end - mp->mem) + 1;
	} else {
		out->len = rem;
		mp->pos = mp->len;
	}
	return end;
}

size_t mp_scan_uint(struct mparser *mp, size_t digits, uintmax_t *val) {
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

size_t mp_scan_int(struct mparser *mp, size_t digits, intmax_t *val) {
	bool sign = false;
	if (curc(mp) == '-') {
		++mp->pos;
		sign = true;
	}
	const size_t k = mp_scan_uint(mp, digits, (uintmax_t *)val);
	if (sign) {
		*val = -*val;
	}
	return k;
}

size_t mp_scan_xint(struct mparser *mp, size_t digits, uintmax_t *val) {
	const uint8_t *pre = mp_slice(mp, 2);
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
	return mp_scan_uint(mp, digits, val);
}


void mp_seek_cur(struct mparser *mp, const ptrdiff_t pos) {
	if (pos < 0) {
		const size_t upos = (size_t)-pos;
		mp->pos = upos > mp->pos ? 0 : mp->pos - upos;
	} else if (SIZE_MAX - mp->pos > (size_t)pos) {
		mp->pos = zumin(mp->pos + (size_t)pos, mp->len);
	} else {
		mp->pos = mp->len;
	}
}

void mp_seek_set(struct mparser *mp, const size_t pos) {
	mp->pos = zumin(pos, mp->len);
}


struct mparser mp_mem(const size_t len, const void *mem) {
	return (struct mparser) {
		.len = len,
		.mem = mem,
	};
}

struct mparser mp_wuptr(const struct wuptr mm) {
	return mp_mem(mm.len, mm.ptr);
}
