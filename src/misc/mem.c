// SPDX-License-Identifier: 0BSD
#include <stdlib.h>
#include <string.h>

#include "misc/math.h"
#include "misc/mem.h"

uint8_t memcycle(uint8_t *dst, const size_t pos) {
	const uint8_t val = dst[pos];
	memmove(dst + 1, dst, pos);
	dst[0] = val;
	return val;
}

void memtessel(void *restrict dst, const void *restrict src, const size_t size,
size_t bytes) {
	uint8_t *d = dst;
	const uint8_t *s = src;
	size_t i = 0;
	const size_t items = bytes/size;
	while (i < items) {
		memcpy(d + i*size, s, size);
		++i;
	}
	memcpy(d + i*size, s, bytes % size);
}

static void u32_set(uint32_t *restrict dst, const void *restrict src,
const size_t nmemb) {
	uint32_t word;
	memcpy(&word, src, sizeof(word));
	for (size_t i = 0; i < nmemb; ++i) {
		dst[i] = word;
	}
}

static void u24_set(uint8_t *restrict dst, const void *restrict src,
const size_t nmemb) {
	const size_t size = 3;
	uint32_t triple;
	memcpy(&triple, src, size);
	size_t i = 0;
	while (i < nmemb - 1) {
		memcpy(dst + i*size, &triple, sizeof(triple));
		++i;
	}
	memcpy(dst + i*size, &triple, size);
}

static void u16_set(uint16_t *dst, const void *restrict src,
const size_t nmemb) {
	uint16_t word;
	memcpy(&word, src, sizeof(word));
	for (size_t i = 0; i < nmemb; ++i) {
		dst[i] = word;
	}
}

void memwordset(void *restrict dst, const void *restrict src,
const size_t size, const size_t nmemb) {
	switch (size) {
	case 1: memset(dst, *((const uint8_t *)src), nmemb); break;
	case 2: u16_set(dst, src, nmemb); break;
	case 3: u24_set(dst, src, nmemb); break;
	case 4: u32_set(dst, src, nmemb); break;
	default: memtessel(dst, src, size, size*nmemb); break;
	}
}

void memrepeat(void *dst, size_t pos, size_t offset, size_t count) {
	uint8_t *d = (uint8_t *)dst + pos;
	const uint8_t *s = d - offset;
	memtessel(d, s, offset, count);
}

void memrepeat_or_zero(void *dst, size_t pos, size_t offset, size_t count) {
	uint8_t *d = dst;
	if (offset > pos + count) {
		memset(d + pos, 0, count);
	} else {
		if (offset > pos) {
			const size_t diff = offset - pos;
			memset(d + pos, 0, diff);
			pos += diff;
			count -= diff;
		}
		memrepeat(dst, pos, offset, count);
	}
}

const void * memchk(const void *s, const unsigned char c, const size_t n) {
	const unsigned char *b = s;
	for (size_t m = 0; m < n; ++m) {
		if (b[m] != c) {
			return b + m;
		}
	}
	return NULL;
}

void * memdup(const void *s, size_t n) {
	void *d = malloc(n);
	if (d) {
		memcpy(d, s, n);
	}
	return d;
}

size_t memccpy_cur(unsigned char *restrict dst,
const unsigned char *restrict src, const unsigned char c, size_t dst_len,
size_t src_len) {
	const size_t n = zumin(dst_len, src_len);
	const unsigned char *end = memccpy(dst, src, c, zumin(dst_len, src_len));
	if (end) {
		return (size_t)(end - dst - 1);
	}
	return n;
}

#ifndef _GNU_SOURCE
void * memrchr(const void *s, const int c, size_t n) {
	const unsigned char *data = s;
	while (n) {
		--n;
		if (data[n] == c) {
			return (void *)(data + n);
		}
	}
	return NULL;
}
#endif
