// SPDX-License-Identifier: 0BSD
#ifndef RASTER_COLOR
#define RASTER_COLOR

#include "cicp.h"
#include "icc.h"
#include "pix.h"
#include "misc/mat.h"

enum color_transfer_fn {
	color_transfer_linear_gamma,
	color_transfer_pq,
	color_transfer_hlg,
};

struct color_transfer {
	enum color_transfer_fn fn;
	float args[5];
};

struct color_convert {
	struct mat43f nonlinear;
	struct color_transfer eotf;
	struct mat3f linear;
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

bool color_space_to_linear_sRGB(const struct color_space *cs,
struct color_convert *conv, enum pix_layout layout, bool maybe_yuv);

cmsHTRANSFORM color_icc_transform(struct color_space *cs, cmsHPROFILE out);

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

bool color_space_is_sRGB(const struct color_space *cs);

#endif /* RASTER_COLOR */
