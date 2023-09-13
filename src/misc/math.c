// SPDX-License-Identifier: 0BSD

#include "misc/math.h"

bool float_ce(const double f1, const double f2) {
	// _ce is for Close Enough
	return fabs(f1 - f2) < 0x1p-14;
}

float fclampf(const float x, const float min, const float max) {
	return fminf(fmaxf(x, min), max);
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

unsigned uclamp(const unsigned n, const unsigned min, const unsigned max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
}

int iclamp(const int n, const int min, const int max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
}
