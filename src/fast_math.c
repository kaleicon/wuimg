// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef WU_FAST_MATH
#define WU_FAST_MATH

#include <math.h>
#include <stdint.h>
#include <string.h>

/* Try to use whichever is best: fmaf() or a naive a*b+c. When supported by
 * the processor, fmaf() is faster, yields better precision, and reduces the
 * amount of instructions, but on unsupported machines it's implemented using
 * doubles, incurring a very dramatic performance hit. We prefer the naive
 * version in those cases.
 * As a fallback we pass the -ffp-contract=fast flag to the compiler, which may
 * turn the naive version into a FMA, but also may not be recognized by all
 * compilers. We could pass =on, but then GCC will certainly ignore it. */
static float fm_fmaf(const float a, const float b, const float c) {
#if FP_FAST_FMAF == 1 || defined(__FMA__) || defined(__FMA4__) || defined(__ARM_FEATURE_FMA)
	return fmaf(a,b,c);
#else
	return a*b+c;
#endif
}

static float fm_mix(const float a, const float b, const float k) {
	/* Linear interpolation. Equivalent to
	 *	a*(1 - k) + b*k
	*/
	return fm_fmaf(b, k, fm_fmaf(a, -k, a));
}

static float fm_fractf(float val) {
	/* Returns the positive fractional part. This is different from
	 * fmod() and modf(), which return negative results for val < 0.
	 * This gets turned into a roundss and subss instruction pair. */
	return val - floorf(val);
}

/* Faster fmax()/fmin() replacements that don't believe in NaNs. These get
 * turned into maxss/minss. */
static float fm_fminf(const float x, const float y) {
	return x < y ? x : y;
}

static float fm_fmaxf(const float x, const float y) {
	return x > y ? x : y;
}

static float fm_fclampf(const float val, const float a, const float b) {
	return fm_fminf(fm_fmaxf(val, a), b);
}

static float fm_saturatef(const float val) {
	return fm_fclampf(val, 0, 1);
}

/* Faster exp2f(), log2f(), and powf() that don't believe in nonsense like
 * NaNs, or negative numbers, or numbers greater than 127.
 * These are less precise than their libc counterparts and don't check for
 * special cases. According to tests, powf(powf(x, 2.2), 1/2.2) scaled to
 * uint16 is off by one ~1% of the time, compared to libc powf.
 * Polynomials were found using Sollya, which should produce more accurate
 * single-precision coefficients than simply truncating high-precision ones.
https://www.sollya.org/
 * The exact script is in scripts/fast_math_polynomials.sollya
*/
static float fm_exp2f_unchecked(float x) {
	/* Build a float equal to 2^(intpart - 127)
	 * x is assumed not to overflow the exponent field. That is, in
	 * range [-127, 128]. */
	const int32_t i = ((int32_t)floorf(x) + 127) << 23;
	float e;
	memcpy(&e, &i, sizeof(i));

	// Get the positive fractional part.
	const float f = fm_fractf(x);

	/* One would normally find a polynomial using
	 *	`fpminimax(2^x, 4, [|single...|], [1/(2^16-1); 1]);`
	 * but in this case the last coefficient (a0) will be very close to 1.
	 * So instead we do
	 *	`fpminimax(2^x, [|1,2,3,4|], [|single...|], [1/(2^16-1); 1], 1);`
	 * so that a0 becomes exactly 1. On its own, adding 1 instead of
	 * 1.000whatever doesn't really saves us anything, but as we've got a
	 * multiply at the end, this lets us fold it into the final FMA. */
	const float //a0 = 0x1p0f,
		a1 = 0x1.62d6c6p-1f,
		a2 = 0x1.ee2454p-3f,
		a3 = 0x1.abf854p-5f,
		a4 = 0x1.b7f75ap-7f;
	return fm_fmaf(fm_fmaf(fm_fmaf(fm_fmaf(a4, f, a3), f, a2), f, a1), f*e, e);
}

static float fm_log2f_for_pow(float x, float mul) {
	/* log2() that accepts the exponent of its powf() parent to save a
	 * single instruction. */

	// Extract the exponent of x
	uint32_t u;
	memcpy(&u, &x, sizeof(u));
	const int32_t i = (int32_t)((u >> 23) & 0xff) - 127;
	const float e = (float)i;

	/* Extract the mantissa, and OR with the binary representation of 1
	 * so that it's 1.fract. */
	const uint32_t one = (0x7f << 23);
	u = (u & 0x7fffff) | one;
	float m;
	memcpy(&m, &u, sizeof(u));

	/* `fpminimax(log2(x)/(x-1), 5, [|single...|], [1; 2]);` */
	const float a0 = 0x1.8ed0dap1f,
		a1 = -0x1.a97a8ep1,
		a2 = 0x1.4ca036p1,
		a3 = -0x1.3b3c36p0,
		a4 = 0x1.45cce8p-2,
		a5 = -0x1.1a0ba8p-5;
	x = fm_fmaf(fm_fmaf(fm_fmaf(fm_fmaf(fm_fmaf(a5, m, a4), m, a3), m, a2), m, a1), m, a0);
	return fm_fmaf(x, fm_fmaf(mul, m, -mul), e * mul);
}

static float fm_exp2f(const float x) {
	// Prevent under- or over-flowing the exponent
	return fm_exp2f_unchecked(fm_fclampf(x, -127, 128));
}

static float fm_powf_unchecked(const float x, const float e) {
	/* If `e` is in range [-127.0/128.0, 1.0], there's no need for clamping
	 * for exp2f */
	return fm_exp2f_unchecked(fm_log2f_for_pow(x, e));
}

static float fm_powf(const float x, const float e) {
	// This doesn't attempt to handle negative x, not even for integer e
	return fm_exp2f(fm_log2f_for_pow(x, e));
}

static float fm_pre_roundf(float val, float scale) {
	/* A dumber and faster roundf() replacement that's worthless for
	 * numbers above 2^22. That's fine since we only care up to 2^16.
	 * On par with lrintf() when FMA is supported, and slightly slower when
	 * not, but not so much as to make us touch some icky global state nor
	 * deal with fesetround() failures. */
	return fm_fmaf(fm_saturatef(val), scale, 0.5f);
}

#endif // WU_FAST_MATH
