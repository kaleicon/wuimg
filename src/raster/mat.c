#include <stdio.h>
#include <math.h>

#include "common/common.h"
#include "raster/mat.h"
#include "raster/unpack.h"

static double dot(const double *restrict row, const double *restrict col,
const int len, const int col_stride) {
	double acc = row[0] * col[0];
	for (int x = 1; x < len; ++x) {
		acc = fma(row[x], col[x*col_stride], acc);
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

void mat_mul_tofloat(float *restrict out, const double *restrict m1,
const double *restrict m2, const int len, const int h1, const int w2) {
	for (int y = 0; y < h1; ++y) {
		vec_mul_mat_tofloat(out + y*w2, m1 + y*len, m2, len, w2);
	}
}

static bool float_ce(const double f1, const double f2) {
	// _ce is for Close Enough
	return fabs(f1 - f2) < 1.0/(1 << 16);
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
/*	out[0] = in[3*1 + 1] * in[3*2 + 2] - in[3*1 + 2] * in[3*2 + 1];
	out[1] = in[3*2 + 1] * in[3*0 + 2] - in[3*2 + 2] * in[3*0 + 1];
	out[2] = in[3*0 + 1] * in[3*1 + 2] - in[3*0 + 2] * in[3*1 + 1];

	out[3] = in[3*1 + 2] * in[3*2 + 0] - in[3*1 + 0] * in[3*2 + 2];
	out[4] = in[3*2 + 2] * in[3*0 + 0] - in[3*2 + 0] * in[3*0 + 2];
	out[5] = in[3*0 + 2] * in[3*1 + 0] - in[3*0 + 0] * in[3*1 + 2];

	out[6] = in[3*1 + 0] * in[3*2 + 1] - in[3*1 + 1] * in[3*2 + 0];
	out[7] = in[3*2 + 0] * in[3*0 + 1] - in[3*2 + 1] * in[3*0 + 0];
	out[8] = in[3*0 + 0] * in[3*1 + 1] - in[3*0 + 1] * in[3*1 + 0];*/

	const double determinant = dot(in, out, 3, 3);
	if (float_ce(determinant, 0)) {
		return false;
	}
	for (size_t i = 0; i < ARRAY_LEN(dst->m); ++i) {
		out[i] *= 1/determinant;
	}
	return true;
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
	unpack_strip(dst, src, len, 64, pix_float, op_pack);
}
