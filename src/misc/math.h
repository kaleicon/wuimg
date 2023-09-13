// SPDX-License-Identifier: 0BSD
#ifndef COMMON_MATH
#define COMMON_MATH

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

bool float_ce(double f1, double f2);

float fclampf(float x, float min, float max);

long lmod(long val, long max);

int imod(int val, int max);

size_t zulog2(size_t x);

unsigned int ulog2(unsigned int x);

size_t zumax(size_t x, size_t y);

size_t zumin(size_t x, size_t y);

unsigned int umax(unsigned int x, unsigned int y);

unsigned int umin(unsigned int x, unsigned int y);

long lmax(long x, long y);

long lmin(long x, long y);

int imax(int x, int y);

int imin(int x, int y);

unsigned uclamp(unsigned n, unsigned min, unsigned max);

int iclamp(int n, int min, int max);

#endif /* COMMON_MATH */
