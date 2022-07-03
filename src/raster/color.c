#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "../common.h"
#include "color.h"
#include "unpack.h"

struct color_xyz {
	double x, y, z;
};

static const double SRGB_GAMMA = 2.4;
static const double SRGB_ALPHA = 0.055;

const char * color_space_type_str(const struct color_space *cs) {
	switch (cs->type) {
	case color_profile_enum: return "Enum";
	case color_profile_custom: return "Custom";
	case color_profile_icc: return "ICC";
	}
	return "???";
}

static void vec_print(const float *vec, const size_t len) {
	for (size_t x = 0; x < len; ++x) {
		printf("%f%c", vec[x], (x == (len-1)) ? '\n' : ',');
	}
}

__attribute__((unused))
static void mat_print(const float *mat, const size_t len) {
	for (size_t y = 0; y < len; ++y) {
		vec_print(mat + y*len, len);
	}
}

static bool float_ce(const float f1, const float f2) {
	// _ce is for Close Enough
	return fabsf(f1 - f2) < 1.0f/(1 << 16);
}

static void mult_vec_mat(float *restrict out, const float *restrict v1,
const float *restrict m2, const int len) {
	for (int x = 0; x < len; ++x) {
		out[x] = 0;
		for (int y = 0; y < len; ++y) {
			out[x] += v1[y] * m2[y*len + x];
		}
	}
}

static void mult_mat(float *restrict out, const float *restrict m1,
const float *restrict m2, const int len) {
	for (int y = 0; y < len; ++y) {
		mult_vec_mat(out + y*len, m1 + y*len, m2, len);
	}
}

static void swizzle_mat(struct color_mat *restrict out,
const struct color_mat *restrict cm, const enum pix_layout swz) {
	float swz_mat[16] = {0};
	for (int y = 0; y < 4; ++y) {
		const int x = pix_layout_offset(swz, y);
		swz_mat[y*4 + x] = 1;
	}

	mult_vec_mat(out->off, cm->off, swz_mat, 4);
	mult_mat(out->mat, cm->mat, swz_mat, 4);
}

static bool mat3_invert(float out[static 9], const float in[static 9]) {
	for (int y = 0; y < 3; ++y) {
		const int o = (y+1)%3;
		const int p = (y+2)%3;
		for (int x = 0; x < 3; ++x) {
			const int m = ((x+1)%3)*3;
			const int n = ((x+2)%3)*3;
			out[y*3 + x] = in[m + o] * in[n + p] - in[m + p] * in[n + o];
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

	const float determinant = out[3*0]*in[0] + out[3*1]*in[1] + out[3*2]*in[2];
	if (float_ce(determinant, 0)) {
		return false;
	}
	for (int i = 0; i < 9; ++i) {
		out[i] *= 1/determinant;
	}
	return true;
}

static void eotf_linear_gamma(struct color_convert *conv, const double alpha,
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

	 * Quite more readable now.
	*/
	const double gm = gamma - 1;
	const double ap = alpha + 1;

	const double over = pow(ap, gamma) * pow(gm, gm);
	const double under = pow(alpha, gm) * pow(gamma, gamma);

	const double a = 1 / ap;
	conv->eotf = color_transfer_linear_gamma;
	conv->args[0] = (float)(alpha / gm);
	conv->args[1] = (float)ap;
	conv->args[2] = (float)(alpha * a);
	conv->args[3] = (float)gamma;
	conv->args[4] = (float)(under / over);
}

static void eotf_sRGB(struct color_convert *conv) {
	/* Famously, sRGB is not continous, and precisely requires rounded
	 * values. */
	const double a = 1 / (1 + SRGB_ALPHA);
	conv->eotf = color_transfer_linear_gamma;
	conv->args[0] = 0.04045f;
	conv->args[1] = (float)a;
	conv->args[2] = (float)(SRGB_ALPHA * a);
	conv->args[3] = (float)SRGB_GAMMA;
	conv->args[4] = (float)(1/12.92);
}

static void eotf_gamma(struct color_convert *conv, const double mult,
const double gamma) {
	conv->eotf = color_transfer_linear_gamma;
	conv->args[0] = 0;
	conv->args[1] = (float)mult;
	conv->args[2] = 0;
	conv->args[3] = (float)gamma;
	conv->args[4] = 1;
}

static void eotf_linear(struct color_convert *conv) {
	conv->eotf = color_transfer_linear_gamma;
	conv->args[0] = 1;
	conv->args[1] = 1;
	conv->args[2] = 0;
	conv->args[3] = 1;
	conv->args[4] = 1;
}

static void eotf_log(struct color_convert *conv, const double cutoff,
const double div) {
	/* In H.273, the logarithmic OETFs are of the form

		if comp > cutoff:
			return log10(comp) / div + 1
		return 0

	 * So the EOTF ought to be

		if comp > eotf_cutoff:
			return pow(10, (comp - 1) * div)
		return comp

	 * Let us remember that pow(x, y) is implemented as exp2(log2(x) * y),
	 * and that exp2 and log2 are typically hardware instructions. Thus:

		logdiv = log2(10) * div
		if comp > eotf_cutoff:
			return exp2((comp - 1) * logdiv)
		return comp
	*/

	const double logdiv = div * log2(10.0);
	conv->eotf = color_transfer_log;
	conv->args[0] = (float)exp2(cutoff * logdiv - logdiv);
	conv->args[1] = (float)logdiv;
}

static void eotf_perceptual_quantization(struct color_convert *conv) {
	/*
		m = 2610 / 16384
		n = 2523 / 4096 * 128
		c = 2392 / 4096 * 32
		b = 2413 / 4096 * 32
		a = c - b + 1 //3424 / 4096 * 32

		ncomp = pow(comp, 1/n)
		return pow(max(ncomp - a, 0) / (b - c*ncomp), 1/m)
	*/
	const double im = 16384.0 / 2610.0;
	const double in = 32.0 / 2523.0;
	const double c = 2392.0 / 128.0;
	const double b = 2413.0 / 128.0;
	conv->eotf = color_transfer_pq;
	conv->args[0] = (float)im;
	conv->args[1] = (float)in;
	conv->args[2] = (float)(c - b + 1);
	conv->args[3] = (float)b;
	conv->args[4] = (float)c;
}

static void eotf_hybrid_log_gamma(struct color_convert *conv) {
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

	 * As another reminder, pow(n, m) * pow(n, o) == pow(n, m + o).
	 * Which for the record means exp2(m) * exp2(o) == exp2(m + o).
	 * Hence, we can fold the (1/12) into iac by adding its logarithm,
	 * thus saving on a constant:

		iac = -(ia * c) // Swap signs for clarity
		iac = iac + log2(1/12)
		return exp2(comp * ia + iac) + bt

	 * Then putting it all together:

		if comp > 1/2:
			return exp2(comp * logia + z) + bt
		return comp * comp * (1/3)
	*/

	const double a = 0.17883277;
	const double b = 1 - 4*a;
	const double c = 0.5 - a*log(4*a);

	const double ia = 1/a * M_LOG2E;
	conv->eotf = color_transfer_hlg;
	conv->args[0] = (float)ia;
	conv->args[1] = (float)( -(ia * c) + log2(1.0/12) );
	conv->args[2] = (float)(b / 12);
}

static bool set_cicp_transfer_data(const enum cicp_transfer transfer,
struct color_convert *conv) {
	switch (transfer) {
	case cicp_transfer_bt709_6:
	case cicp_transfer_bt601_7:
	case cicp_transfer_iec_61966_2_4: /* Additionally defined for negative
		 * inputs. The curve should mirror at 0, so it's up to the code
		 * to do abs() and copysign() */
	case cicp_transfer_bt1361_0: /* Defined also for inputs < 0 and > 1,
		 * but the curve is complicated, so TODO. */
	case cicp_transfer_bt2020_2_10bit:
	case cicp_transfer_bt2020_2_12bit:
		// https://www.itu.int/rec/R-REC-BT.2020/en
		eotf_linear_gamma(conv, 0.0993, 1/0.45);
		return true;
	case cicp_transfer_unspecified:
		break;
	case cicp_transfer_bt470_6_system_m:
		eotf_gamma(conv, 1, 2.2);
		return true;
	case cicp_transfer_bt470_6_system_b_g:
		eotf_gamma(conv, 1, 2.8);
		return true;
	case cicp_transfer_smpte_st_240:
		/* H.273 gives us only this OETF, with comp being the input
		 * value, and all other variables unknown.

			if comp > cutoff:
				return alpha * pow(comp, 0.45) - (alpha - 1)
			return comp * 4.0

		 * The relevant standard is paywalled, so we've assumed it's a
		 * normal linear-gamma function, and obtained alpha from phi.
		*/
		eotf_linear_gamma(conv, 0.11157219592173123, 1/0.45);
		return true;
	case cicp_transfer_linear:
		eotf_linear(conv);
		return true;
	case cicp_transfer_log:
		eotf_log(conv, 0.01, 2);
		return true;
	case cicp_transfer_log_sqrt:
		eotf_log(conv, sqrt(10) / 1000, 2.5);
		return true;
	case cicp_transfer_iec_61966_2_1:
		/* With matrix coef == 0, uses the sRGB EOTF.
		 * With matrix coef == 5, uses the sYCC EOTF, which is like
		 * sRGB but defined for negative inputs, similar to
		 * IEC 61966-2-4 above. */
		eotf_sRGB(conv);
		return true;
	case cicp_transfer_smpte_st_2084:
		// https://www.itu.int/rec/R-REC-BT.2100/en
		eotf_perceptual_quantization(conv);
		return true;
	case cicp_transfer_smpte_st_428_1:
		/*
			return pow(comp, 2.6) * 52.37 / 48
		 * becomes
			return pow(comp * pow(52.37 / 48, 1/2.6), 2.6)
		*/
		eotf_gamma(conv, pow(52.37 / 48, 1/2.6), 2.6);
		return true;
	case cicp_transfer_arib_std_b67:
		eotf_hybrid_log_gamma(conv);
		return true;
	}
	return false;
}

static float range_offset(const bool limited) {
	return (limited) ? -1.0/16 : 0;
}

static float range_scaler(const bool limited) {
	return (limited) ? (float)(255.0/(235-16)) : 1.0;
}

static void gen_mat_ycgco(struct color_mat *cm, const bool limited) {
	const float o = range_offset(limited);
	const float s = range_scaler(limited);
	*cm = (struct color_mat) {
		.off = {o, o, o, 0},
		.mat = {
			s,  s, -s, 0,
			s,  0,  s, 0,
			s, -s, -s, 0,
			0,  0,  0, 1,
		}
	};
}

static void gen_mat_ycbcr(struct color_mat *cm, const bool limited,
const double b, const double r) {
	const float lum = range_scaler(limited);
	const double chr = (limited) ? 255.0/(240-16) : 1.0;
	const double mg = -1.0 + b + r; // minus green

	const double two_b = (2 - 2*b) * chr;
	const double two_r = (2 - 2*r) * chr;

	*cm = (struct color_mat) {
		.off = {
			range_offset(limited),
			-.5,
			-.5,
			0,
		},
		.mat = {
			lum, 0,                     (float)(two_r),        0,
			lum, (float)(b/mg * two_b), (float)(r/mg * two_r), 0,
			lum, (float)(two_b),        0,                     0,
			0,   0,                     0,                     1,
		},
	};
}

static void gen_mat_rgb(struct color_mat *cm, const bool limited) {
	const float o = range_offset(limited);
	const float s = range_scaler(limited);
	*cm = (struct color_mat) {
		.off = {o, o, o, 0},
		.mat = {
			s, 0, 0, 0,
			0, s, 0, 0,
			0, 0, s, 0,
			0, 0, 0, 1
		}
	};
}

static const struct color_primaries * get_cicp_primaries(
const enum cicp_primaries primaries) {
	static const struct color_xy white_d65 = {
		.3127, .3290
	};
	static const struct color_primaries bt709 = {
		.w = white_d65,
		.r = {.64, .33},
		.g = {.30, .60},
		.b = {.15, .06},
	};
	static const struct color_primaries system_m = {
		.w = {.310, .316},
		.r = {.67, .33},
		.g = {.21, .71},
		.b = {.14, .08},
	};
	static const struct color_primaries system_b_g = {
		.w = white_d65,
		.r = {.64, .33},
		.g = {.29, .60},
		.b = {.15, .06},
	};
	static const struct color_primaries bt601 = {
		.w = white_d65,
		.r = {.630, .340},
		.g = {.310, .595},
		.b = {.155, .070},
	};
	static const struct color_primaries generic_film = {
		.w = {.310, .316},
		.r = {.681, .319},
		.g = {.243, .692},
		.b = {.145, .049},
	};
	static const struct color_primaries bt2020 = {
		.w = white_d65,
		.r = {.708, .292},
		.g = {.170, .797},
		.b = {.131, .046},
	};
	static const struct color_primaries st_428 = {
		.w = {(float)1/3, (float)1/3},
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
		.w = white_d65,
		.r = {.680, .320},
		.g = {.265, .690},
		.b = {.150, .060},
	};
	static const struct color_primaries nightmare_inducing = {
		.w = white_d65,
		.r = {.630, .340},
		.g = {.295, .605},
		.b = {.155, .077},
	};
	switch (primaries) {
	case cicp_primaries_bt709_6:
		return &bt709;
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
	case cicp_primaries_nightmare_inducing:
		return &nightmare_inducing;
	}
	return NULL;
}

static const struct color_primaries * get_primaries(
const struct color_space *cs) {
	switch (cs->type) {
	case color_profile_enum:
		return get_cicp_primaries(cs->primaries);
	case color_profile_custom:
		return &cs->desc->u.prof.pri;
	case color_profile_icc:
		break;
	}
	return NULL;
}

static double primary_z(const struct color_xy xy) {
	return 1.0 - (xy.x + xy.y);
}

static struct color_xyz full_primaries(const struct color_xy xy) {
	return (struct color_xyz) {xy.x, xy.y, primary_z(xy)};
}

static bool kb_kr_from_chroma(double *restrict kb, double *restrict kr,
const struct color_space *cs) {
	const struct color_primaries *p = get_primaries(cs);
	if (p) {
		const struct color_xyz w = full_primaries(p->w);
		const struct color_xyz r = full_primaries(p->r);
		const struct color_xyz g = full_primaries(p->g);
		const struct color_xyz b = full_primaries(p->b);

		const double gb_yz = g.y * b.z - b.y * g.z;
		const double rg_yz = r.y * g.z - g.y * r.z;
		const double div = w.y * (
			r.x * gb_yz
			+ g.x * (b.y * r.z - r.y * b.z)
			+ b.x * rg_yz
		);
		const double b_num = b.y * (
			w.x * rg_yz
			+ w.y * (g.x * r.z - r.x * g.z)
			+ w.z * (r.y * g.z - g.y * r.z)
		);
		const double r_num = r.z * (
			w.y * gb_yz
			+ w.y * (b.y * g.z - g.y * b.z)
			+ w.z * (r.y * b.z - b.y * r.z)
		);
		*kb = b_num / div;
		*kr = r_num / div;
	}
	return (bool)p;
}

static bool gen_mat(struct color_mat *cm, const struct color_space *cs) {
	double b, r;
	switch (cs->matrix) {
	case cicp_matrix_rgb:
		gen_mat_rgb(cm, cs->limited);
		return true;
	case cicp_matrix_bt709_6:
		b = .0722;
		r = .2126;
		break;
	case cicp_matrix_unspecified:
		return false;
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
		gen_mat_ycgco(cm, cs->limited);
		return true;
	case cicp_matrix_bt2020_2_nonconstant:
		b = .0593;
		r = .2627;
		break;
	case cicp_matrix_bt2020_2_constant: // TODO
	case cicp_matrix_smpte_st_2085: // TODO
		return false;
	case cicp_matrix_chroma_derived_nonconstant:
		if (!kb_kr_from_chroma(&b, &r, cs)) {
			return false;
		}
		break;
	case cicp_matrix_chroma_derived_constant: // TODO
	case cicp_matrix_bt2100_2_icpct: // TODO
		return false;
	}
	gen_mat_ycbcr(cm, cs->limited, b, r);
	return true;
}

void color_mat_gen(struct color_mat *out, const struct color_space *cs,
const enum pix_layout swizzle) {
	struct color_mat cm;
	struct color_mat *ptr = (swizzle != pix_rgba) ? &cm : out;
	if (swizzle == pix_gray || !gen_mat(ptr, cs)) {
		gen_mat_rgb(ptr, cs->limited);
	}
	if (ptr != out) {
		swizzle_mat(out, ptr, swizzle);
	}
}

static void mat3_set_primaries(float out[static 9],
const struct color_xy pri[static 3]) {
	for (size_t x = 0; x < 3; ++x) {
		out[x] = (float)pri[x].x;
		out[3+x] = (float)pri[x].y;
		out[3*2+x] = (float)primary_z(pri[x]);
	}
}

static void vec3_set_primaries(float vec[static 3], const struct color_xy xy) {
	vec[0] = (float)xy.x;
	vec[1] = (float)xy.y;
	vec[2] = (float)primary_z(xy);
}

static void rgb_to_XYZ(float out[static 9], const struct color_primaries *pri) {
	mat3_set_primaries(out, &pri->r);

	float inv[3*3];
	mat3_invert(inv, out);

	float white_point[3];
	vec3_set_primaries(white_point, pri->w);

	float scale[3];
	mult_vec_mat(scale, white_point, inv, 3);
	for (int y = 0; y < 3; ++y) {
		for (int x = 0; x < 3; ++x) {
			out[y*3 + x] *= scale[x];
		}
	}
}

static void XYZ_to_rgb(float out[static 9], const struct color_primaries *pri) {
	float tmp[3*3];
	rgb_to_XYZ(tmp, pri);
	mat3_invert(out, tmp);
}

static bool set_transfer_data(const struct color_space *cs,
struct color_convert *conv) {
	switch (cs->type) {
	case color_profile_enum:
		if (set_cicp_transfer_data(cs->transfer, conv)) {
			return true;
		}
		break;
	case color_profile_custom:
		if (!set_cicp_transfer_data(cs->transfer, conv)) {
			struct color_transfer *xfer = &cs->desc->u.prof.xfer;
			eotf_gamma(conv, 1, xfer->r);
		}
		return true;
	case color_profile_icc:
		break;
	}
	return false;
}

bool color_space_to_linear_sRGB(const struct color_space *cs,
struct color_convert *conv) {
	if (!set_transfer_data(cs, conv)) {
		eotf_sRGB(conv);
	}
	const struct color_primaries *pri = get_primaries(cs);
	if (pri) {
		float in[9];
		rgb_to_XYZ(in, pri);
		float out[9];
		XYZ_to_rgb(out, get_cicp_primaries(cicp_primaries_bt709_6));

		mult_mat(conv->mat, in, out, 3);
	}
	return (bool)pri;
}

cmsHTRANSFORM color_icc_transform(struct color_space *cs, cmsHPROFILE out) {
	struct icc_profile *c = &cs->desc->u.icc;
	if (!c->transform) {
		c->transform = cmsCreateTransform(c->in, TYPE_RGB_8,
			out, TYPE_RGB_16, INTENT_PERCEPTUAL, 0);
	}
	return c->transform;
}

static cmsCIExyY primary_to_xyY(const struct color_xy xy) {
	return (cmsCIExyY) {xy.x, xy.y, 1.0};
}

cmsHPROFILE color_icc_linear_sRGB(void) {
	cmsHPROFILE out = NULL;
	cmsToneCurve *crv = cmsBuildGamma(NULL, 1.0);
	if (crv) {
		const struct color_primaries *pri = get_cicp_primaries(
			cicp_primaries_bt709_6);
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

static void set_transfer_triple(struct color_transfer *xfer, const double gamma) {
	xfer->r = xfer->g = xfer->b = gamma;
}

static struct color_space_desc * get_or_init_desc(struct color_space *cs,
const enum color_profile_type type) {
	if (cs->type == color_profile_enum) {
		struct color_space_desc *desc = calloc(1, sizeof(*cs->desc));
		if (desc) {
			if (type == color_profile_custom) {
				set_transfer_triple(&desc->u.prof.xfer,
					SRGB_GAMMA);
				desc->u.prof.pri = *get_cicp_primaries(
					cicp_primaries_bt709_6);
			}
			cs->desc = desc;
			cs->type = type;
		}
	} else if (cs->type != type) {
		return NULL;
	}
	return cs->desc;
}

static struct color_profile * get_or_init_profile(struct color_space *cs) {
	struct color_space_desc *desc = get_or_init_desc(cs, color_profile_custom);
	if (desc) {
		return &cs->desc->u.prof;
	}
	return NULL;
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

bool color_space_set_gamma_rgb(struct color_space *cs, const double r,
const double g, const double b) {
	struct color_profile *prof = get_or_init_profile(cs);
	if (prof) {
		cs->transfer = 0;
		prof->xfer.r = r;
		prof->xfer.g = g;
		prof->xfer.b = b;
	}
	return prof;
}

bool color_space_set_gamma(struct color_space *cs, const double gamma) {
	struct color_profile *prof = get_or_init_profile(cs);
	if (prof) {
		cs->transfer = 0;
		set_transfer_triple(&prof->xfer, gamma);
	}
	return prof;
}

bool color_space_set_primaries_rgb(struct color_space *cs, double rx, double ry,
double gx, double gy, double bx, double by) {
	struct color_profile *prof = get_or_init_profile(cs);
	if (prof) {
		cs->primaries = 0;
		struct color_primaries *pri = &prof->pri;
		pri->r.x = (float)rx;
		pri->r.y = (float)ry;
		pri->g.x = (float)gx;
		pri->g.y = (float)gy;
		pri->b.x = (float)bx;
		pri->b.y = (float)by;
	}
	return prof;
}

bool color_space_set_primaries(struct color_space *cs, double wx, double wy,
double rx, double ry, double gx, double gy, double bx, double by) {
	struct color_profile *prof = get_or_init_profile(cs);
	if (prof) {
		cs->primaries = 0;
		prof->pri = (struct color_primaries) {
			.w = {(float)wx, (float)wy},
			.r = {(float)rx, (float)ry},
			.g = {(float)gx, (float)gy},
			.b = {(float)bx, (float)by},
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
