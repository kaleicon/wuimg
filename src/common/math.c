
#include "common/math.h"

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

int iclamp(const int n, const int min, const int max) {
	if (n < min) {
		return min;
	} else if (n > max) {
		return max;
	}
	return n;
}
