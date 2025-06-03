// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef RASTER_COLOR
#define RASTER_COLOR

#include "misc/mat.h"
#include "raster/alpha.h"
#include "raster/cicp.h"
#include "raster/icc.h"

enum color_transfer_fn {
	color_transfer_linear_gamma,
	color_transfer_pq,
	color_transfer_hlg,
};

struct color_transfer {
	enum color_transfer_fn fn:8;
	bool srgb_input;
	float args[5];
};

struct color_map {
	float mul[4];
	float add[4];
};

enum color_steps {
	color_step_map = 1 << 0,
	color_step_nonlinear = 1 << 1,
	color_step_eotf = 1 << 2,
	color_step_linear = 1 << 3,
	color_step_icc = 1 << 4,
};

struct color_convert {
	enum color_steps steps;
	struct color_map map;
	struct mat3f nonlinear;
	struct color_transfer eotf;
	struct mat3f linear;
};

enum color_white_point {
	color_white_other = 0,
	color_white_d65,
	color_white_c,
	color_white_e,
	color_white_dci,
};

struct color_xy {
	double x, y;
};

struct color_primaries {
	struct color_xy w, r, g, b;
};

struct color_gamma {
	double r, g, b;
};

struct color_profile {
	struct color_gamma gamma;
	struct color_primaries pri;
};

struct color_space_desc {
	int refs;
	union {
		struct color_profile prof;
		struct icc_profile icc;
	} u;
};

enum color_profile_type {
	color_profile_enum = 0,
	color_profile_custom,
	color_profile_icc,
};

struct color_space {
	enum cicp_primaries primaries:8;
	enum cicp_transfer transfer:8;
	enum cicp_matrix matrix:8;
	bool limited;

	enum color_profile_type type:8;
	struct color_space_desc *desc;
};

const char * color_space_type_str(const struct color_space *cs);

double color_space_get_gamma(const struct color_space *cs);

enum color_white_point color_space_white_point_type(const struct color_space *cs);

const struct color_primaries * color_space_get_primaries(
const struct color_space *cs);

bool color_space_to_linear_sRGB(const struct color_space *cs,
struct color_convert *conv, bool grayscale, bool maybe_yuv, double scale);

cmsHTRANSFORM color_icc_transform(const struct color_space *cs, cmsHPROFILE out,
cmsUInt32Number in_fmt, cmsUInt32Number out_fmt);

cmsHPROFILE color_icc_linear_sRGB(void);

bool color_space_set_icc_copy(struct color_space *cs, const void *restrict data,
size_t len);

bool color_space_set_icc_owned(struct color_space *cs, void *restrict data,
size_t len);

bool color_space_set_gamma_rgb(struct color_space *cs, double r, double g,
double b);

bool color_space_set_gamma(struct color_space *cs, double gamma);

bool color_space_set_primaries_rgb(struct color_space *cs, double rx, double ry,
double gx, double gy, double bx, double by);

bool color_space_set_primaries_whitepoint(struct color_space *cs,
double wx, double wy);

bool color_space_set_primaries(struct color_space *cs, double wx, double wy,
double rx, double ry, double gx, double gy, double bx, double by);

void color_space_unref(struct color_space *cs);

struct color_space color_space_ref(struct color_space *orig);

#endif /* RASTER_COLOR */
