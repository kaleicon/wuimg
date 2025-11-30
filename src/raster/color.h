// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef RASTER_COLOR
#define RASTER_COLOR

#include "misc/mat.h"
#include "raster/alpha.h"
#include "raster/cicp.h"
#include "raster/icc.h"

static const double COLOR_SRGB_DISPLAY_GAMMA = 2.2;

enum color_transfer_fn {
	/* EOTF:
		if comp > arg0:
			return pow(comp * arg1 + arg2, arg3)
		return comp * arg4
	 * OETF:
		if comp > arg0:
			return pow(comp, arg3) * arg1 + arg2
		return comp * arg4
	*/
	color_transfer_linear_gamma,

	/* EOTF:
		ncomp = pow(comp, arg4)
		num = max(ncomp - arg1, 0)
		den = arg2 - arg3 * ncomp
		return pow(num / den, arg0)
	 * OETF:
		ncomp = pow(comp, arg0)
		num = arg1 + arg2 * ncomp
		den = 1 + arg3 * ncomp
		return pow(num / den, arg4)
	*/
	color_transfer_pq,

	/* EOTF:
		if comp > arg0:
			return exp2(comp * arg1 + arg2) + arg3
		return comp * comp * arg4
	 * OETF:
		if comp > arg0:
			return log2(comp + arg3) * arg1 + arg2
		return sqrt(comp * arg4)
	*/
	color_transfer_hlg,
};

struct color_transfer {
	enum color_transfer_fn fn:8;
	bool invert_input;
	bool srgb; // True if this is the sRGB EOTF
	float args[5];
};

// Bitfield indicating which steps modify the input
enum color_steps {
	// Normalize input type and bitrange into [0.0, 1.0]
	color_step_normalize = 1 << 0,
	// Matrix multiplication or range remap in nonlinear (electrical) space
	color_step_nonlinear = 1 << 1,
	// Conversion to linear (optical) space
	color_step_eotf = 1 << 2,
	// Matrix multiplication in linear space
	color_step_linear = 1 << 3,
	// Conversion to nonlinear space
	color_step_oetf = 1 << 4,
	// Input uses an ICC profile
	color_step_icc = 1 << 5,
};

struct color_convert {
	enum color_steps steps;
	float color_offset[3];
	float alpha_map[2];
	struct mat3f nonlinear;
	struct color_transfer eotf;
	struct mat3f linear;
	float lum_scale;
	struct color_transfer oetf;
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
	int refs;
	struct color_gamma gamma;
	struct color_primaries pri;
};

enum color_profile_type {
	color_profile_enum = 0,
	color_profile_param,
	color_profile_icc,
};

struct color_space {
	enum cicp_primaries primaries:8;
	enum cicp_transfer transfer:8;
	enum cicp_matrix matrix:8;
	bool limited:1;
	bool invert:1;
	bool invert_alpha:1;
	enum color_profile_type type:2;
	struct color_space_luminance {
		uint16_t max, ref;
	} lum;
	union color_space_desc {
		struct color_profile *prof;
		struct icc_file *icc;
	} desc;
};

const char * color_space_type_str(const struct color_space *cs);

enum color_white_point color_space_white_point_type(const struct color_space *cs);

struct color_space_luminance color_space_get_luminance(const struct color_space *cs);

double color_space_get_gamma(const struct color_space *cs);

const struct color_primaries * color_space_get_primaries(
const struct color_space *cs);

void color_space_walk(const struct color_space *restrict cs,
const struct color_space *restrict tgt, struct color_convert *conv,
bool grayscale, bool maybe_yuv, double scale);

void color_space_to_sRGB(const struct color_space *cs,
struct color_convert *conv, bool grayscale, bool maybe_yuv, double scale);

struct icc_transform * color_icc_transform(const struct color_space *cs,
const struct icc_profile *out, uint32_t in_fmt, uint32_t out_fmt);

struct icc_profile * color_icc_linear_sRGB(void);

bool color_space_is_sRGB(const struct color_space *cs);

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
