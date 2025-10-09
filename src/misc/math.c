// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "misc/math.h"

uint32_t uadd8_32(const uint32_t x, const uint32_t y) {
	const uint32_t mask = 0x80808080;
	const uint32_t sum = (x & ~mask) + (y & ~mask);
	return sum ^ (x & mask) ^ (y & mask);
}

size_t zuceildiv(const size_t x, const size_t y) {
	return (x + y - 1) / y;
}

long lmod(const long val, const long max) {
	return (val % max + max) % max;
}

int imod(const int val, const int max) {
	return (val % max + max) % max;
}

size_t zulog2(size_t x) {
	size_t acc = 0;
	while ((x >>= 1)) {
		++acc;
	}
	return acc;
}

unsigned int ulog2(unsigned int x) {
	unsigned int acc = 0;
	while ((x >>= 1)) {
		++acc;
	}
	return acc;
}

size_t zumax(const size_t x, const size_t y) {
	return x > y ? x : y;
}
size_t zumin(const size_t x, const size_t y) {
	return x < y ? x : y;
}

uint32_t u32max(const uint32_t x, const uint32_t y) {
	return x > y ? x : y;
}
uint32_t u32min(const uint32_t x, const uint32_t y) {
	return x < y ? x : y;
}

unsigned int umax(const unsigned int x, const unsigned int y) {
	return x > y ? x : y;
}
unsigned int umin(const unsigned int x, const unsigned int y) {
	return x < y ? x : y;
}

long lmax(const long x, const long y) {
	return x > y ? x : y;
}
long lmin(const long x, const long y) {
	return x < y ? x : y;
}

int imax(const int x, const int y) {
	return x > y ? x : y;
}
int imin(const int x, const int y) {
	return x < y ? x : y;
}

float fclampf(const float x, const float min, const float max) {
	return fminf(fmaxf(x, min), max);
}

int iclamp(const int n, const int min, const int max) {
	return imin(imax(n, min), max);
}
