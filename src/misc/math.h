// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef COMMON_MATH
#define COMMON_MATH

#include <math.h>
#include <stddef.h>
#include <stdint.h>

static inline uint32_t uadd8_32(const uint32_t x, const uint32_t y) {
	const uint32_t mask = 0x80808080;
	const uint32_t sum = (x & ~mask) + (y & ~mask);
	return sum ^ (x & mask) ^ (y & mask);
}

static inline size_t zuceildiv(const size_t x, const size_t y) {
	return (x + y - 1) / y;
}

static inline long lmod(const long val, const long max) {
	return (val % max + max) % max;
}
static inline int imod(const int val, const int max) {
	return (val % max + max) % max;
}

static inline size_t zulog2(size_t x) {
	size_t acc = 0;
	while ((x >>= 1)) {
		++acc;
	}
	return acc;
}
static inline unsigned int ulog2(unsigned int x) {
	unsigned int acc = 0;
	while ((x >>= 1)) {
		++acc;
	}
	return acc;
}

static inline size_t zumax(const size_t x, const size_t y) {
	return x > y ? x : y;
}
static inline size_t zumin(const size_t x, const size_t y) {
	return x < y ? x : y;
}

static inline uint32_t u32max(const uint32_t x, const uint32_t y) {
	return x > y ? x : y;
}
static inline uint32_t u32min(const uint32_t x, const uint32_t y) {
	return x < y ? x : y;
}

static inline unsigned int umax(const unsigned int x, const unsigned int y) {
	return x > y ? x : y;
}
static inline unsigned int umin(const unsigned int x, const unsigned int y) {
	return x < y ? x : y;
}

static inline long lmax(const long x, const long y) {
	return x > y ? x : y;
}
static inline long lmin(const long x, const long y) {
	return x < y ? x : y;
}

static inline int imax(const int x, const int y) {
	return x > y ? x : y;
}
static inline int imin(const int x, const int y) {
	return x < y ? x : y;
}

static inline float fclampf(const float x, const float min, const float max) {
	return fminf(fmaxf(x, min), max);
}

static inline int iclamp(const int n, const int min, const int max) {
	return imin(imax(n, min), max);
}
#endif /* COMMON_MATH */
