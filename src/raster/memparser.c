#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "../common.h"
#include "memparser.h"

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

static bool bndchk(const struct mem_parser *mp) {
	return mp->pos < mp->len;
}

static unsigned char curc(const struct mem_parser *mp) {
	return mp->mem[mp->pos];
}


unsigned char mem_next_char_unsafe(struct mem_parser *mp) {
	const unsigned char c = curc(mp);
	++mp->pos;
	return c;
}

size_t mem_get_uint_unsafe(struct mem_parser *mp, size_t digits,
mem_fast_t *val) {
	*val = 0;
	size_t k = 0;
	while (k < digits && isdigit(curc(mp))) {
		*val = *val * 10 - '0' + curc(mp);
		++mp->pos;
		++k;
	}
	return k;
}

void mem_skip_blank(struct mem_parser *mp) {
	while (bndchk(mp) && isblank(curc(mp))) {
		++mp->pos;
	}
}

void mem_skip_space(struct mem_parser *mp) {
	while (bndchk(mp) && isspace(curc(mp))) {
		++mp->pos;
	}
}

void mem_skip_nonspace(struct mem_parser *mp) {
	while (bndchk(mp) && !isspace(curc(mp))) {
		++mp->pos;
	}
}

void mem_skip_line(struct mem_parser *mp) {
	const unsigned char *newline = memchr(mp->mem + mp->pos, '\n',
		mp->len - mp->pos);
	if (newline) {
		mp->pos = (size_t)newline - (size_t)mp->mem;
	} else {
		mp->pos = mp->len;
	}
}

int mem_next_char(struct mem_parser *mp) {
	if (bndchk(mp)) {
		return mem_next_char_unsafe(mp);
	}
	return EOF;
}

int mem_next_nonblank(struct mem_parser *mp) {
	mem_skip_blank(mp);
	return mem_next_char(mp);
}

int mem_next_nonspace(struct mem_parser *mp) {
	mem_skip_space(mp);
	return mem_next_char(mp);
}

struct wuptr mem_next_remaining(struct mem_parser *mp, const size_t len) {
	const size_t rem = zumin(mp->len - mp->pos, len);
	const struct wuptr wp = wuptr_mem(mp->mem + mp->pos, rem);
	mp->pos += rem;
	return wp;
}

const uint8_t * mem_next_slice(struct mem_parser *mp, const size_t len) {
	if (mp->len - mp->pos >= len) {
		const uint8_t *slice = mp->mem + mp->pos;
		mp->pos += len;
		return slice;
	}
	return NULL;
}

struct wuptr mem_get_word(struct mem_parser *mp) {
	const size_t start = mp->pos;
	mem_skip_nonspace(mp);
	return wuptr_mem(mp->mem + start, mp->pos - start);
}

size_t mem_get_uint(struct mem_parser *mp, size_t digits, mem_fast_t *val) {
	const size_t rem = mp->len - mp->pos;
	if (rem < digits) {
		digits = rem;
	}
	return mem_get_uint_unsafe(mp, digits, val);
}

static bool scan_end(struct mem_parser *mp, const size_t start,
const size_t size, const bool read, const uint_fast64_t num, void *restrict val) {
	uint_fast64_t max;
	switch (size) {
	case 1: max = UINT8_MAX; break;
	case 2: max = UINT16_MAX; break;
	case 4: max = UINT32_MAX; break;
	case 8: max = UINT64_MAX; break;
	default:
		mp->pos = start;
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
	mp->pos = start;
	return false;
}

bool mem_scan_xint(struct mem_parser *mp, const size_t size, void *restrict val) {
	const size_t start = mp->pos;
	if (mem_next_char(mp) != '0' || toupper(mem_next_char(mp)) != 'X') {
		mp->pos = start;
		return false;
	}

	bool read = false;
	uint_fast64_t num = 0;
	while (bndchk(mp) && isxdigit(curc(mp))) {
		const uint_fast64_t before = num;
		num = num * 16 + (uint_fast64_t)toxnum(mem_next_char_unsafe(mp));
		if (num < before) {
			mp->pos = start;
			return false;
		}
		read = true;
	}
	return scan_end(mp, start, size, read, num, val);
}

bool mem_scan_uint(struct mem_parser *mp, const size_t size, void *restrict val) {
	const size_t start = mp->pos;
	bool read = false;
	uint_fast64_t num = 0;
	while (bndchk(mp) && isdigit(curc(mp))) {
		const uint_fast64_t before = num;
		num = num * 10 - '0' + mem_next_char_unsafe(mp);
		if (num < before) {
			mp->pos = start;
			return false;
		}
		read = true;
	}
	return scan_end(mp, start, size, read, num, val);
}

struct mem_parser mem_parser_mem(const size_t len, const void *mem) {
	return (struct mem_parser) {
		.len = len,
		.mem = mem,
	};
}
