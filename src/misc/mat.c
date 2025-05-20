// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "misc/common.h"
#include "misc/mat.h"
#include "misc/math.h"
#include "raster/unpack.h"

void matf_mul(float *restrict out, const float *restrict m1,
const float *restrict m2, const int len, const int h1, const int w2) {
	for (int y = 0; y < h1; ++y) {
		for (int x = 0; x < w2; ++x) {
			float acc = m1[y*len] * m2[x];
			for (int i = 1; i < len; ++i) {
				acc = fmaf(m1[y*len + i], m2[x + i*w2], acc);
			}
			out[y*w2 + x] = acc;
		}
	}
}

static double dot(const double *restrict row, const double *restrict col,
const int len, const int col_stride) {
	double acc = row[0] * col[0];
	for (int i = 1; i < len; ++i) {
		acc = fma(row[i], col[i*col_stride], acc);
	}
	return acc;
}

void vec_mul_mat(double *restrict out, const double *restrict v1,
const double *restrict m2, const int len, const int w2) {
	for (int x = 0; x < w2; ++x) {
		out[x] = dot(v1, m2 + x, len, w2);
	}
}

static void vec_mul_mat_tofloat(float *restrict out, const double *restrict v1,
const double *restrict m2, const int len, const int w2) {
	for (int x = 0; x < w2; ++x) {
		out[x] = (float)dot(v1, m2 + x, len, w2);
	}
}


void mat_mul(double *restrict out, const double *restrict m1,
const double *restrict m2, const int len, const int h1, const int w2) {
	for (int y = 0; y < h1; ++y) {
		vec_mul_mat(out + y*w2, m1 + y*len, m2, len, w2);
	}
}


void mat_mul_tofloat(float *restrict out, const double *restrict m1,
const double *restrict m2, const int len, const int h1, const int w2) {
	for (int y = 0; y < h1; ++y) {
		vec_mul_mat_tofloat(out + y*w2, m1 + y*len, m2, len, w2);
	}
}

void mati_mul(int *restrict out, const int *restrict m1,
const int *restrict m2, const int len, const int h1, const int w2) {
	for (int y = 0; y < h1; ++y) {
		for (int x = 0; x < w2; ++x) {
			int acc = m1[y*len] * m2[x];
			for (int i = 1; i < len; ++i) {
				acc += m1[y*len + i] * m2[x + i*w2];
			}
			out[y*w2 + x] = acc;
		}
	}
}


static double fms(const double x, const double y, const double z) {
	return fma(x, y, -z);
}

bool mat3_invert(struct mat3 *restrict dst, const struct mat3 *restrict src) {
	double *out = dst->m;
	const double *in = src->m;
	for (int y = 0; y < 3; ++y) {
		const int o = (y+1)%3;
		const int p = (y+2)%3;
		for (int x = 0; x < 3; ++x) {
			const int m = ((x+1)%3)*3;
			const int n = ((x+2)%3)*3;
			out[y*3 + x] = fms(in[m + o], in[n + p],
				in[m + p] * in[n + o]);
		}
	}

	const double determinant = dot(in, out, 3, 3);
	if (isnormal(determinant)) {
		for (size_t i = 0; i < ARRAY_LEN(dst->m); ++i) {
			out[i] *= 1/determinant;
		}
		return true;
	}
	return false;
}

void matf_identity(float *mat, const size_t w, const size_t h) {
	for (size_t y = 0; y < h; ++y) {
		for (size_t x = 0; x < w; ++x) {
			mat[y*w + x] = (x == y);
		}
	}
}

void vecf_print(const float *vec, const size_t len, FILE *out) {
	for (size_t x = 0; x < len; ++x) {
		fprintf(out, "%f%c", vec[x], (x+1 == len) ? '\n' : ',');
	}
}

void matf_print(const float *mat, const size_t w, const size_t h, FILE *out) {
	for (size_t y = 0; y < h; ++y) {
		vecf_print(mat + y*w, w, out);
	}
}

double cross_idx(const double *restrict v1, const double *restrict v2,
const int i) {
	const int m = (i+1)%3;
	const int n = (i+2)%3;
	return fms(v1[m], v2[n], v1[n] * v2[m]);
}

void float_from_double(float *dst, const double *src, const size_t len) {
	unpack_strip(dst, src, len, 64, pix_float, op_pack, NULL);
}
