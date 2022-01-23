#include <string.h>
#include <stdbool.h>

#include "color.h"
#include "unpack.h"

const char * color_space_str(const enum color_space cs) {
	switch (cs) {
	case color_space_rgb: return "rgba";
	case color_space_ycbcr:
	case color_space_ycbcr_limited:
		return "yuva";
	}
	return "????";
}

static void mat4_mult(float out[static 4], const float m1[static 4],
const float m2[static 16]) {
	for (int x = 0; x < 4; ++x) {
		out[x] = m1[0] * m2[x]
			+ m1[1] * m2[4*1 + x]
			+ m1[2] * m2[4*2 + x]
			+ m1[3] * m2[4*3 + x];
	}
}

static void swizzle_mat(struct color_mat *restrict out,
const struct color_mat *restrict cm, const enum pix_layout swz) {
	float swz_mat[16] = {0};
	for (int y = 0; y < 4; ++y) {
		const int x = pix_layout_offset(swz, y);
		swz_mat[y*4 + x] = 1;
	}

	mat4_mult(out->off, cm->off, swz_mat);
	for (int y = 0; y < 4; ++y) {
		mat4_mult(out->mat + y*4, cm->mat + y*4, swz_mat);
	}
}

static void color_mat_ycbcr(struct color_mat *cm, const enum color_space cs) {
	/* BT.601 YCbCr */
	const bool er = (cs == color_space_ycbcr_limited);

	const double b = 0.114;
	const double r = 0.299;
	const double mg = 1.0 / (-1.0 + b + r);

	const float lum = (er) ? (float)(255.0/(235-16)) : 1.0;
	const double chr = (er) ? 255.0/(240-16) : 1.0;

	const double two_b = (2 - 2*b) * chr;
	const double two_r = (2 - 2*r) * chr;

	*cm = (struct color_mat) {
		.off = {
			(er) ? (float)(-1.0/16) : 0.0,
			-0.5,
			-0.5,
			0,
		},
		.mat = {
			lum, 0,                     (float)(two_r),        0,
			lum, (float)(b*mg * two_b), (float)(r*mg * two_r), 0,
			lum, (float)(two_b),        0,                     0,
			0,   0,                     0,                     1,
		},
	};
}

static void color_mat_rgb(struct color_mat *cm) {
	memset(cm, 0, sizeof(*cm));
	cm->mat[0] = cm->mat[4+1] = cm->mat[4*2+2] = cm->mat[4*3+3] = 1;
}

void color_mat_gen(struct color_mat *out, const enum color_space space,
const enum pix_layout swizzle) {
	struct color_mat cm;
	struct color_mat *ptr = (swizzle != pix_rgba) ? &cm : out;
	switch (space) {
	case color_space_ycbcr:
	case color_space_ycbcr_limited:
		color_mat_ycbcr(ptr, space);
		break;
	case color_space_rgb:
		color_mat_rgb(ptr);
		break;
	}

	if (swizzle != pix_rgba) {
		swizzle_mat(out, &cm, swizzle);
	}
}
