// SPDX-License-Identifier: 0BSD
#ifndef RASTER_MAT
#define RASTER_MAT

#include <stdbool.h>

struct mat3f {
	float m[3*3];
};

struct mat43f {
	float m[4*3];
};

struct mat3 {
	double m[3*3];
};

struct mat43 {
	double m[4*3];
};

struct mat2i {
	int m[2*2];
};

void vec_mul_mat(double *restrict out, const double *restrict v1,
const double *restrict m2, int len, int w2);

void mat_mul(double *restrict out, const double *restrict m1,
const double *restrict m2, int len, int h1, int w2);

void mat_mul_tofloat(float *restrict out, const double *restrict m1,
const double *restrict m2, int len, int h1, int w2);

void mati_mul(int *restrict out, const int *restrict m1,
const int *restrict m2, int len, int h1, int w2);

bool mat3_invert(struct mat3 *restrict dst, const struct mat3 *restrict src);

void matf_identity(float *mat, size_t w, size_t h);

void vecf_print(const float *vec, size_t len, FILE *out);

void matf_print(const float *mat, size_t w, size_t h, FILE *out);

double cross_idx(const double *restrict v1, const double *restrict v2, int i);

void float_from_double(float *dst, const double *src, size_t len);

#endif /* RASTER_MAT */
