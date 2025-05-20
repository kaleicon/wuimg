// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "misc/common.h"
#include "raster/color.h"

#define WHITE_D65 {.3127, .3290}
#define WHITE_C {.3101, .3162}
static const struct color_primaries SRGB_PRIMARIES = {
	.w = WHITE_D65,
	.r = {.64, .33},
	.g = {.30, .60},
	.b = {.15, .06},
};
static const double SRGB_GAMMA = 2.4;
static const double SRGB_ALPHA = 0.055;

enum simple_mat {
	simple_mat_rgb,
	simple_mat_gray,
	simple_mat_ycgco,
};

struct color_map_double {
	double mul[3];
	double add[3];
};

const char * color_space_type_str(const struct color_space *cs) {
	switch (cs->type) {
	case color_profile_enum: return "Enum";
	case color_profile_custom: return "Custom";
	case color_profile_icc: return "ICC";
	}
	return "???";
}

static void eotf_linear_gamma(struct color_transfer *eotf, const double alpha,
const double gamma) {
	/* We set the arguments to an EOTF with a linear part and a power
	 * part. This function is of the form
		if comp > cutoff:
			return pow( (comp + alpha) / (1 + alpha), gamma )
		return comp / phi

	 * comp is the color component, and alpha and gamma are chosen by
	 * the encoding standard. cutoff is where the linear and power
	 * segments meet with continuity of slope, and it is calculated thus:

		cutoff = alpha / (gamma - 1)

	 * while phi is:

		gm = gamma - 1
		over = pow(1 + alpha, gamma) * pow(gm, gm)
		under = pow(alpha, gm) * pow(gamma, gamma)
		phi = over / under

	 * Since in today's processors math is not instantaneous, and division
	 * is specially slow, we calculate some values beforehand:

		a = 1 / (1 + alpha)
		b = alpha * a
		d = 1 / phi

	 * and redefine the EOTF thus:

		if comp > cutoff:
			return pow(comp * a + b, gamma)
		return comp * d
	*/
	const double gm = gamma - 1;
	const double ap = alpha + 1;

	const double over = pow(ap, gamma) * pow(gm, gm);
	const double under = pow(alpha, gm) * pow(gamma, gamma);

	*eotf = (struct color_transfer) {
		.fn = color_transfer_linear_gamma,
		.args = {
			(float)(alpha / gm),
			(float)ap,
			(float)(alpha / ap),
			(float)gamma,
			(float)(under / over),
		},
	};
}

static void eotf_sRGB(struct color_transfer *eotf) {
	/* Famously, sRGB is not continous, and precisely requires rounded
	 * values. */
	const double a = 1 + SRGB_ALPHA;
	*eotf = (struct color_transfer) {
		.fn = color_transfer_linear_gamma,
		.srgb_input = true,
		.args = {
			0.04045f,
			(float)(1/a),
			(float)(SRGB_ALPHA / a),
			(float)SRGB_GAMMA,
			(float)(1/12.92),
		},
	};
}

static void eotf_gamma(struct color_transfer *eotf, const double mul,
const double gamma) {
	*eotf = (struct color_transfer) {
		.fn = color_transfer_linear_gamma,
		.args = {0, (float)mul, 0, (float)gamma, 1},
	};
}

static void eotf_linear(struct color_transfer *eotf) {
	*eotf = (struct color_transfer) {
		.fn = color_transfer_linear_gamma,
		.args = {1, 1, 0, 1, 1},
	};
}

static void eotf_log(struct color_transfer *eotf, const double cutoff,
const double div) {
	/* In H.273, the logarithmic OETFs are of the form

		if comp > cutoff:
			return log10(comp) / div + 1
		return 0

	 * where cutoff is where the output becomes 0, as tipped off by the
	 * else branch. This means that any valid input to the corresponding
	 * EOTF will be positive, its cutoff 0, and so a branch is not needed.
	 * Thus the EOTF ought to be:

		return pow(10, (comp - 1) * div)

	 * Let us remember that pow(x, y) is really exp2(log2(x) * y). Thus,
	 * this is actually:

		return exp2(log2(10) * (comp - 1) * div)

	 * Simplified:

		logdiv = log2(10) * div
		return exp2((comp - 1) * logdiv)

	 * With FMA:

		return exp2(comp * logdiv - logdiv)
	*/

	(void)cutoff;
	const float logdiv = (float)(div * log2(10.0));
	*eotf = (struct color_transfer) {
		// The HLG function can be repurposed for this, so why not
		.fn = color_transfer_hlg,
		.args = {0, logdiv, -logdiv, 0, 0},
	};
}

static void eotf_perceptual_quantization(struct color_transfer *eotf) {
	/*
		m = 2610 / 16384
		n = 2523 / 4096 * 128
		c = 2392 / 4096 * 32
		b = 2413 / 4096 * 32
		a = c - b + 1 // alternatively (3424 / 4096)

		ncomp = pow(comp, 1/n)
		num = max(ncomp - a, 0)
		den = b - c*ncomp
		return pow(num / den, 1/m)
	*/
	const double inv_m = 16384.0 / 2610.0;
	const double inv_n = 32.0 / 2523.0;
	const double c = 2392.0 / 128.0;
	const double b = 2413.0 / 128.0;
	const double a = 3424.0 / 4096.0;
	*eotf = (struct color_transfer) {
		.fn = color_transfer_pq,
		.args = {
			(float)inv_n,
			(float)a,
			(float)b,
			(float)c,
			(float)inv_m,
		},
	};
}

static void eotf_hybrid_log_gamma(struct color_transfer *eotf) {
	/* As per BT.2100, the EOTF is given as the inverse of the OETF,
	 * and it's basically:
		if comp > 1/2:
			return (exp((comp - c) / a) + b) / 12
		return pow(comp, 2) / 3

	 * With a = 0.17883277, b = 1 - 4*a, and c = 0.5 - a*log(4*a).
	 * Let us remember that exp(n) is pow(e, n), and that itself is
	 * exp2(log2(e) * n). Thus:

		ia = (1 / a) * log2(e)
		return (exp2((comp - c) * ia) + b) * (1/12)

	 * Use fused-multiply-adds

		iac = ia * c
		return (exp2(comp * ia - iac) + b) * (1/12)

	 * Then more fma

		bt = b / 12
		return exp2(comp * ia - iac) * (1/12) + bt

	 * As another reminder, exp2(m) * exp2(o) == exp2(m + o).
	 * Hence, we can fold the (1/12) into iac by adding its logarithm,
	 * thus saving on a constant:

		iacl = -iac + log2(1/12)
		return exp2(comp * ia + iacl) + bt

	 * Putting it all together:

		if comp > 1/2:
			return exp2(comp * ia + iacl) + bt
		return comp * comp * (1/3)
	*/

	const double a = 0.17883277;
	const double b = fma(4, -a, 1);
	const double c = fma(log(4*a), -a, 0.5);

	const double ia = M_LOG2E / a;
	const double bt = b / 12;
	*eotf = (struct color_transfer) {
		.fn = color_transfer_hlg,
		.args = {
			(float)(1.0/2.0),
			(float)ia,
			(float)( fma(ia, -c, log2(1.0/12.0)) ),
			(float)bt,
			(float)(1.0/3.0),
		},
	};
}

static bool set_cicp_eotf(const enum cicp_transfer transfer,
struct color_transfer *eotf) {
	switch (transfer) {
	case cicp_transfer_bt709_6:
	case cicp_transfer_bt601_7: // Same as BT.709
	case cicp_transfer_iec_61966_2_4: /* Same as above, but additionally
		 * defined for negative inputs. Since the curve is symmetric,
		 * it can be implemented with abs() on input and copysign() on
		 * output. */
	case cicp_transfer_bt1361_0: /* As BT.709, but extended to the range
		 * [-0.25,1.33]. Values >= -0.0045 are a simple extension of
		 * the curve, but the region < -0.0045 would require a third
		 * branch. I believe we should simply forget about it.

		 * But for completeness, comp < oetf(-0.0045) is
			alpha = 0.0993
			gamma = 1/0.45
			c = (comp*-4 + alpha) / (alpha+1)
			return pow(c, gamma) / -4
		 */
	case cicp_transfer_bt2020_2_10bit:
	case cicp_transfer_bt2020_2_12bit: // https://www.itu.int/rec/R-REC-BT.2020/en
		eotf_linear_gamma(eotf, 0.09929682680944, 1/0.45);
		return true;
	case cicp_transfer_unspecified:
		break;
	case cicp_transfer_bt470_6_system_m:
		eotf_gamma(eotf, 1, 2.2);
		return true;
	case cicp_transfer_bt470_6_system_b_g:
		eotf_gamma(eotf, 1, 2.8);
		return true;
	case cicp_transfer_smpte_st_240:
		/* H.273 gives us only this OETF, with comp being the input
		 * value, and all other variables unknown.

			if comp > cutoff:
				return alpha * pow(comp, 0.45) - (alpha - 1)
			return comp * 4.0

		 * SMPTE stuff is all paywalled, so we've assumed it's a
		 * normal linear-gamma function, and obtained alpha from phi.
		 * Hopefully we're close. */
		eotf_linear_gamma(eotf, 0.11157219592173123, 1/0.45);
		return true;
	case cicp_transfer_linear:
		eotf_linear(eotf);
		return true;
	case cicp_transfer_log:
		eotf_log(eotf, 0.01, 2);
		return true;
	case cicp_transfer_log_sqrt:
		eotf_log(eotf, sqrt(10) / 1000, 2.5);
		return true;
	case cicp_transfer_iec_61966_2_1:
		/* With matrix coef == 0, uses the sRGB EOTF.
		 * With matrix coef == 5, uses the sYCC EOTF, which is the same
		 * but extended to negative inputs. */
		eotf_sRGB(eotf);
		return true;
	case cicp_transfer_smpte_st_2084:
		// https://www.itu.int/rec/R-REC-BT.2100/en
		eotf_perceptual_quantization(eotf);
		return true;
	case cicp_transfer_smpte_st_428_1:
		/* The function is
			return pow(comp, 2.6) * 52.37 / 48
		 * This can be turned into
			return pow(comp * pow(52.37 / 48, 1/2.6), 2.6)
		 * which lets us reuse the normal gamma code.
		*/
		eotf_gamma(eotf, pow(52.37 / 48, 1/2.6), 2.6);
		return true;
	case cicp_transfer_arib_std_b67:
		eotf_hybrid_log_gamma(eotf);
		return true;
	}
	return false;
}

static const struct color_primaries * get_cicp_primaries(
const enum cicp_primaries primaries, const enum cicp_matrix matrix,
const struct color_primaries *fallback) {
	static const struct color_primaries system_m = {
		.w = WHITE_C,
		.r = {.67, .33},
		.g = {.21, .71},
		.b = {.14, .08},
	};
	static const struct color_primaries system_b_g = {
		.w = WHITE_D65,
		.r = {.64, .33},
		.g = {.29, .60},
		.b = {.15, .06},
	};
	static const struct color_primaries bt601 = {
		.w = WHITE_D65,
		.r = {.630, .340},
		.g = {.310, .595},
		.b = {.155, .070},
	};
	static const struct color_primaries generic_film = {
		.w = WHITE_C,
		.r = {.681, .319},
		.g = {.243, .692},
		.b = {.145, .049},
	};
	static const struct color_primaries bt2020 = {
		.w = WHITE_D65,
		.r = {.708, .292},
		.g = {.170, .797},
		.b = {.131, .046},
	};
	static const struct color_primaries st_428 = {
		.w = {1/3, 1/3},
		.r = {1, 0},
		.g = {0, 1},
		.b = {0, 0},
	};
	static const struct color_primaries rp_431 = {
		.w = {.314, .351},
		.r = {.680, .320},
		.g = {.265, .690},
		.b = {.150, .060},
	};
	static const struct color_primaries eg_432 = {
		.w = WHITE_D65,
		.r = {.680, .320},
		.g = {.265, .690},
		.b = {.150, .060},
	};
	static const struct color_primaries unidentified = {
		.w = WHITE_D65,
		.r = {.630, .340},
		.g = {.295, .605},
		.b = {.155, .077},
	};

	if (matrix == cicp_matrix_bt2100_2_ictcp) {
		return &bt2020;
	}

	switch (primaries) {
	case cicp_primaries_bt709_6:
		return &SRGB_PRIMARIES;
	case cicp_primaries_unspecified:
		break;
	case cicp_primaries_bt470_6_system_m:
		return &system_m;
	case cicp_primaries_bt470_6_system_b_g:
		return &system_b_g;
	case cicp_primaries_bt601_7:
	case cicp_primaries_smpte_st_240:
		return &bt601;
	case cicp_primaries_generic_film:
		return &generic_film;
	case cicp_primaries_bt2020_2:
		return &bt2020;
	case cicp_primaries_smpte_st_428_1:
		return &st_428;
	case cicp_primaries_smpte_rp_431_2:
		return &rp_431;
	case cicp_primaries_smpte_eg_432_1:
		return &eg_432;
	case cicp_primaries_the_unidentified:
		return &unidentified;
	}
	return fallback;
}

static const struct color_primaries * get_primaries(
const struct color_space *cs, const struct color_primaries *fallback) {
	switch (cs->type) {
	case color_profile_enum:
		return get_cicp_primaries(cs->primaries, cs->matrix, fallback);
	case color_profile_custom:
		return &cs->desc->u.prof.pri;
	case color_profile_icc:
		break;
	}
	return fallback;
}

static double range_offset(const bool limited) {
	return (limited) ? -1.0/16 : 0;
}

static double range_diff_scaler(const bool limited) {
	return (limited) ? 255.0/(240-16) : 1;
}

static double range_scaler(const bool limited) {
	return (limited) ? 255.0/(235-16) : 1;
}

static void lum_chroma_scale(struct color_map_double *m, double lum_mul,
double chr_mul, double lum_add, double chr_add) {
	m->mul[0] = lum_mul;
	m->mul[1] = chr_mul;
	m->mul[2] = chr_mul;
	m->add[0] = lum_add;
	m->add[1] = chr_add;
	m->add[2] = chr_add;
}

static void gen_mat_simple(struct mat3 *in, struct color_map_double *m,
const bool limited, const enum simple_mat type) {
	for (size_t i = 0; i < 3; ++i) {
		m->mul[i] = range_scaler(limited);
		m->add[i] = range_offset(limited);
	}
	switch (type) {
	case simple_mat_rgb:
		*in = (struct mat3) {
			.m = {
				1, 0, 0,
				0, 1, 0,
				0, 0, 1,
			}
		};
		break;
	case simple_mat_gray:
		*in = (struct mat3) {
			.m = {
				1, 1, 1,
				0, 0, 0,
				0, 0, 0,
			}
		};
		break;
	case simple_mat_ycgco:
		*in = (struct mat3) {
			.m = {
				1,  1,  1,
				1,  0, -1,
				-1, 1, -1,
			}
		};
		break;
	}
}

static void gen_mat_ycbcr(struct mat3 *in, struct color_map_double *map,
const bool limited, const double b, const double r) {
	const double lum = 1;
	const double mg = -1.0 + b + r; // minus green

	const double two_b = fma(-2, b, 2); // 2 - 2*b
	const double two_r = fma(-2, r, 2); // 2 - 2*r

	lum_chroma_scale(map, range_scaler(limited), range_diff_scaler(limited),
		range_offset(limited), -.5);
	*in = (struct mat3) {
		.m = {
			lum,   lum,            lum,
			0,     (b/mg * two_b), two_b,
			two_r, (r/mg * two_r), 0,
		},
	};
}

static void gen_mat_ydzdx(struct mat3 *in, struct color_map_double *m,
const bool limited) {
	/* SMPTE stuff is all paywalled, but H.273 gives us (all values
	 * non-linear):
		Y = G
		Dz = (0.986566 * B - Y) / 2.0
		Dx = (R - 0.991902 * Y) / 2.0

	 * where the components are scaled and offset in the same way YCbCr is.
	 * The inverse ought to be
		(Dz * 2.0 + Y) / 0.986566 = B
		Dx * 2.0 + 0.991902 * Y   = R

	 * As tipped off by the names, the output is actually in the X'Y'Z'
	 * (that is, gamma-encoded) color space.
	 * TODO: Test.
	*/

	lum_chroma_scale(m, range_scaler(limited), range_diff_scaler(limited) * 2,
		range_offset(limited), -.5);
	const double s = 1;
	const double d = 2;
	const double vz = 0.986566;
	const double vx = 0.991902;

	*in = (struct mat3) {
		.m = { // out-X'    out-Y'  out-Z'
			(s * vx),   s,      (s / vz), // in-Y
			0,          0,      (d / vz), // in-Dz
			d,          0,      0,        // in-Dx
		}
	};
}

static void gen_mat_ictcp(struct mat3 *in, struct color_map_double *m,
const bool limited, const enum cicp_transfer transfer) {
	/* ICtCp is derived by
		LMS = RGB * mat
		L'M'S' = oetf(LMS)
		ICtCp = L'M'S' * diff_mat

	 * where diff_mat depends on whether the transfer function is HLG or PQ.
	 * diff_mat is equivalent to the following:

		I = 0.5*L' + 0.5*M'
		PQ:
			Ct = (6610*L' - 13613*M' + 7003*S') / 4096
			Cp = (17933*L' - 17390*M' - 543*S') / 4096
		HLG:
			Ct = (3625*L' - 7465*M' + 3840*S') / 4096
			Cp = (9500*L' - 9212*M' - 288*S') / 4096
	*/
	lum_chroma_scale(m, range_scaler(limited)/2, range_diff_scaler(limited)/4096,
		range_offset(limited), -.5);
	const double i = 1;
	const double d = 1;

	struct mat3 from_lms;
	if (transfer == cicp_transfer_smpte_st_2084) { // PQ
		from_lms = (struct mat3) {
			.m = {
				i, d*6610, d*17933,
				i, -d*13613, -d*17390,
				0, d*7003, -d*543
			},
		};
	} else { // HLG, hopefully
		from_lms = (struct mat3) {
			.m = {
				i, d*3625, d*9500,
				i, -d*7465, -d*9212,
				0, d*3840, -d*288,
			},
		};
	}
	mat3_invert(in, &from_lms);
}

static double primary_z(const struct color_xy xy) {
	return 1.0 - (xy.x + xy.y);
}

static void set_primaries_stride(double *vec, const struct color_xy xy,
const size_t stride) {
	vec[0] = xy.x;
	vec[stride] = xy.y;
	vec[stride*2] = primary_z(xy);
}

static void mat3_set_primaries(struct mat3 *out,
const struct color_primaries *p) {
	set_primaries_stride(out->m, p->r, 3);
	set_primaries_stride(out->m+1, p->g, 3);
	set_primaries_stride(out->m+2, p->b, 3);
}

static void vec3_set_primaries(double vec[static 3], const struct color_xy xy) {
	set_primaries_stride(vec, xy, 1);
}

static bool kb_kr_from_chroma(double *restrict kb, double *restrict kr,
const struct color_space *cs) {
	const struct color_primaries *p = get_primaries(cs, NULL);
	if (p) {
		double w[3], r[3], g[3], b[3];
		vec3_set_primaries(w, p->w);
		vec3_set_primaries(r, p->r);
		vec3_set_primaries(g, p->g);
		vec3_set_primaries(b, p->b);

		const double rg_0 = cross_idx(r, g, 0);
		const double gb_0 = cross_idx(g, b, 0);
		const double r_num = r[1] * (
			fma(w[2], cross_idx(g, b, 2),
				fma(w[1], cross_idx(g, b, 1),
					w[0] * gb_0
				)
			)
		);
		const double b_num = b[1] * (
			fma(w[2], cross_idx(r, g, 2),
				fma(w[1], cross_idx(r, g, 1),
					w[0] * rg_0
				)
			)
		);
		const double div = w[1] * (
			fma(b[0], rg_0,
				fma(g[0], cross_idx(b, r, 0),
					r[0] * gb_0
				)
			)
		);
		*kb = b_num / div;
		*kr = r_num / div;
	}
	return (bool)p;
}

static bool gen_mat(struct mat3 *in, struct color_map_double *map,
const struct color_space *cs, const bool assume_yuv) {
	double b = 0;
	double r = 0;
	switch (cs->matrix) {
	case cicp_matrix_rgb:
		gen_mat_simple(in, map, cs->limited, simple_mat_rgb);
		return true;
	case cicp_matrix_unspecified:
		if (!assume_yuv) {
			gen_mat_simple(in, map, cs->limited, simple_mat_rgb);
			return true;
		}
		// fallthrough
	case cicp_matrix_bt709_6:
		b = .0722;
		r = .2126;
		break;
	case cicp_matrix_fcc_title_47:
		b = .11;
		r = .3;
		break;
	case cicp_matrix_bt470_6_system_b_g:
	case cicp_matrix_bt601_7:
		b = .114;
		r = .299;
		break;
	case cicp_matrix_smpte_st_240:
		b = .087;
		r = .212;
		break;
	case cicp_matrix_ycgco:
		gen_mat_simple(in, map, cs->limited, simple_mat_ycgco);
		return true;
	case cicp_matrix_bt2020_2_nonconstant:
		b = .0593;
		r = .2627;
		break;
	case cicp_matrix_bt2020_2_constant:
	case cicp_matrix_chroma_derived_constant:
		/* Constant-luminance requires branching, so a matrix is not
		 * powerful enough for the task. TODO. */
		return false;
	case cicp_matrix_smpte_st_2085:
		gen_mat_ydzdx(in, map, cs->limited);
		return true;
	case cicp_matrix_chroma_derived_nonconstant:
		if (!kb_kr_from_chroma(&b, &r, cs)) {
			return false;
		}
		break;
	case cicp_matrix_bt2100_2_ictcp:
		/* Output is L'M'S' colorspace. */
		gen_mat_ictcp(in, map, cs->limited, cs->transfer);
		return true;
	}
	gen_mat_ycbcr(in, map, cs->limited, b, r);
	return true;
}

static void color_mat_gen(const struct color_space *cs,
struct color_convert *conv, const bool grayscale, const bool assume_yuv,
const double scale) {
	struct mat3 cm;
	struct color_map_double map;
	if (grayscale) {
		gen_mat_simple(&cm, &map, cs->limited, simple_mat_gray);
	} else if (!gen_mat(&cm, &map, cs, assume_yuv)) {
		gen_mat_simple(&cm, &map, cs->limited, simple_mat_rgb);
	}

	/* Input is meant to be offset then scaled. Optimize so we can use
	 * fused-multiply-adds instead. */
	for (size_t i = 0; i < 3; ++i) {
		conv->map.mul[i] = (float)(map.mul[i] * scale);
		conv->map.add[i] = (float)(map.add[i] * map.mul[i]);
		conv->steps |= (conv->map.mul[i] != 1 && conv->map.add[i] != 0)
			? color_step_map : 0;
	}
	conv->map.mul[3] = (float)scale;
	conv->map.add[3] = 0;
	conv->steps |= (conv->map.mul[3] != 1) ? color_step_map : 0;
	for (size_t i = 0; i < ARRAY_LEN(conv->nonlinear.m); ++i) {
		conv->nonlinear.m[i] = (float)cm.m[i];
		conv->steps |= (conv->nonlinear.m[i] != (i % 4 == 0))
			? color_step_nonlinear : 0;
	}
}

static bool set_eotf(const struct color_space *cs, struct color_transfer *eotf) {
	switch (cs->type) {
	case color_profile_enum:
		return set_cicp_eotf(cs->transfer, eotf);
	case color_profile_custom:
		if (!set_cicp_eotf(cs->transfer, eotf)) {
			struct color_gamma *gamma = &cs->desc->u.prof.gamma;
			eotf_gamma(eotf, 1, gamma->r);
		}
		return true;
	case color_profile_icc:
		break;
	}
	return false;
}

static void rgb_to_XYZ(struct mat3 *out, const struct color_primaries *pri) {
	mat3_set_primaries(out, pri);

	struct mat3 inv;
	mat3_invert(&inv, out);

	double white_point[3];
	vec3_set_primaries(white_point, pri->w);

	double scale[3];
	vec_mul_mat(scale, white_point, inv.m, 3, 3);
	for (int y = 0; y < 3; ++y) {
		for (int x = 0; x < 3; ++x) {
			out->m[y*3 + x] *= scale[x];
		}
	}
}

static void XYZ_to_rgb(struct mat3 *out, const struct color_primaries *pri) {
	struct mat3 tmp;
	rgb_to_XYZ(&tmp, pri);
	mat3_invert(out, &tmp);
}

static void LMS_to_bt2020_rgb(struct mat3 *out) {
	const double s = 4096;
	const struct mat3 lms = {
		.m = {
			1688/s, 2146/s, 262/s,
			683/s, 2951/s, 462/s,
			99/s, 309/s, 3688/s,
		}
	};
	mat3_invert(out, &lms);
}

static bool is_linear_rgb(const enum cicp_matrix matrix) {
	switch (matrix) {
	case cicp_matrix_smpte_st_2085:
	case cicp_matrix_bt2100_2_ictcp:
		return false;
	default: break;
	}
	return true;
}

static bool primaries_close_to_bt709(const struct color_primaries *pri) {
	const double *p = (double *)pri;
	const double *s = (double *)&SRGB_PRIMARIES;
	// This is the max possible error introduced by PNG fixed point format
	const double max_diff = 1.0/100000;
	for (size_t i = 0; i < sizeof(*pri) / sizeof(*p); ++i) {
		if (fabs(*p - *s) > max_diff) {
			return false;
		}
	}
	return true;
}

bool color_space_to_linear_sRGB(const struct color_space *cs,
struct color_convert *conv, const bool grayscale, const bool maybe_yuv,
const double scale) {
	conv->steps |= (cs->type == color_profile_icc) ? color_step_icc : 0;
	color_mat_gen(cs, conv, grayscale, maybe_yuv, scale);
	if (!set_eotf(cs, &conv->eotf)) {
		eotf_sRGB(&conv->eotf);
	}
	conv->steps |= (conv->eotf.fn == color_transfer_linear_gamma
		&& conv->eotf.args[1] == 1
		&& conv->eotf.args[2] == 0
		&& conv->eotf.args[3] == 1
		&& conv->eotf.args[4] == 1) ? 0 : color_step_eotf;

	const struct color_primaries *pri = get_primaries(cs, &SRGB_PRIMARIES);
	if (is_linear_rgb(cs->matrix) && primaries_close_to_bt709(pri)) {
		matf_identity(conv->linear.m, 3, 3);
	} else {
		conv->eotf.srgb_input = false;
		struct mat3 out;
		XYZ_to_rgb(&out, &SRGB_PRIMARIES);
		if (cs->matrix == cicp_matrix_smpte_st_2085) {
			float_from_double(conv->linear.m, out.m,
				ARRAY_LEN(conv->linear.m));
		} else {
			struct mat3 in;
			rgb_to_XYZ(&in, pri);
			if (cs->matrix == cicp_matrix_bt2100_2_ictcp) {
				struct mat3 to_rgb, tmp;
				LMS_to_bt2020_rgb(&to_rgb);
				mat_mul(tmp.m, to_rgb.m, in.m, 3, 3, 3);
				mat_mul_tofloat(conv->linear.m, tmp.m, out.m,
					3, 3, 3);
			} else {
				mat_mul_tofloat(conv->linear.m, in.m, out.m,
					3, 3, 3);
			}
		}
		conv->steps |= color_step_linear;
	}
	return (bool)pri;
}

cmsHTRANSFORM color_icc_transform(const struct color_space *cs, cmsHPROFILE out,
const cmsUInt32Number in_fmt, const cmsUInt32Number out_fmt) {
	return cmsCreateTransform(cs->desc->u.icc.in, in_fmt,
			out, out_fmt, INTENT_PERCEPTUAL, cmsFLAGS_COPY_ALPHA);
}

static cmsCIExyY primary_to_xyY(const struct color_xy xy) {
	return (cmsCIExyY) {xy.x, xy.y, 1.0};
}

cmsHPROFILE color_icc_linear_sRGB(void) {
	cmsHPROFILE out = NULL;
	cmsToneCurve *crv = cmsBuildGamma(NULL, 1.0);
	if (crv) {
		const struct color_primaries *pri = &SRGB_PRIMARIES;
		const cmsCIExyY w = primary_to_xyY(pri->w);
		const cmsCIExyYTRIPLE rgb = {
			.Red = primary_to_xyY(pri->r),
			.Green = primary_to_xyY(pri->g),
			.Blue = primary_to_xyY(pri->b),
		};
		cmsToneCurve *transfer[3] = {crv, crv, crv};
		out = cmsCreateRGBProfile(&w, &rgb, transfer);
		cmsFreeToneCurve(crv);
	}
	return out;
}

static void set_transfer_triple(struct color_gamma *xfer, const double gamma) {
	xfer->r = xfer->g = xfer->b = gamma;
}

static struct color_space_desc * get_or_init_desc(struct color_space *cs,
const enum color_profile_type type) {
	if (cs->type == color_profile_enum) {
		struct color_space_desc *desc = calloc(1, sizeof(*cs->desc));
		if (desc) {
			if (type == color_profile_custom) {
				cs->transfer = 0;
				set_transfer_triple(&desc->u.prof.gamma,
					SRGB_GAMMA);
				desc->u.prof.pri = *get_cicp_primaries(
					cs->primaries, cs->matrix,
					&SRGB_PRIMARIES);
			}
			cs->desc = desc;
			cs->type = type;
		}
	} else if (cs->type != type) {
		return NULL;
	}
	return cs->desc;
}

bool color_space_set_icc_copy(struct color_space *cs, const void *restrict data,
const size_t len) {
	struct color_space_desc *desc = get_or_init_desc(cs, color_profile_icc);
	if (desc) {
		return icc_profile_mem_copy(&desc->u.icc, data, len);
	}
	return false;
}

bool color_space_set_icc_owned(struct color_space *cs, void *restrict data,
const size_t len) {
	struct color_space_desc *desc = get_or_init_desc(cs, color_profile_icc);
	if (desc) {
		return icc_profile_mem_own(&desc->u.icc, data, len);
	}
	free(data);
	return false;
}

static struct color_profile * get_or_init_profile(struct color_space *cs) {
	struct color_space_desc *desc = get_or_init_desc(cs, color_profile_custom);
	if (desc) {
		return &cs->desc->u.prof;
	}
	return NULL;
}

bool color_space_set_gamma_rgb(struct color_space *cs, const double r,
const double g, const double b) {
	struct color_profile *prof = get_or_init_profile(cs);
	if (prof) {
		cs->transfer = 0;
		prof->gamma.r = r;
		prof->gamma.g = g;
		prof->gamma.b = b;
	}
	return prof;
}

bool color_space_set_gamma(struct color_space *cs, const double gamma) {
	return color_space_set_gamma_rgb(cs, gamma, gamma, gamma);
}

bool color_space_set_primaries_rgb(struct color_space *cs, double rx, double ry,
double gx, double gy, double bx, double by) {
	struct color_profile *prof = get_or_init_profile(cs);
	if (prof) {
		struct color_primaries *pri = &prof->pri;
		pri->r.x = rx;
		pri->r.y = ry;
		pri->g.x = gx;
		pri->g.y = gy;
		pri->b.x = bx;
		pri->b.y = by;
	}
	return prof;
}

bool color_space_set_primaries_whitepoint(struct color_space *cs,
const double wx, const double wy) {
	struct color_profile *prof = get_or_init_profile(cs);
	if (prof) {
		struct color_primaries *pri = &prof->pri;
		pri->w.x = wx;
		pri->w.y = wy;
	}
	return prof;
}

bool color_space_set_primaries(struct color_space *cs, double wx, double wy,
double rx, double ry, double gx, double gy, double bx, double by) {
	struct color_profile *prof = get_or_init_profile(cs);
	if (prof) {
		prof->pri = (struct color_primaries) {
			.w = {wx, wy},
			.r = {rx, ry},
			.g = {gx, gy},
			.b = {bx, by},
		};
	}
	return prof;
}

void color_space_unref(struct color_space *cs) {
	struct color_space_desc *desc = cs->desc;
	if (desc) {
		if (desc->refs) {
			--desc->refs;
		} else {
			if (cs->type == color_profile_icc) {
				icc_profile_free(&desc->u.icc);
			}
			free(desc);
		}
	}
}

struct color_space color_space_ref(struct color_space *orig) {
	if (orig->desc) {
		++orig->desc->refs;
	}
	return *orig;
}
