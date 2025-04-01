// SPDX-License-Identifier: 0BSD
#include "misc/bit.h"
#include "misc/common.h"
#include "misc/math.h"
#include "imgconv.h"

// Set to true for a somewhat faster powf(), otherwise use libc functions
static const uint8_t USE_MATH_APPROX = false;

void imgconv_close(struct imgconv *state) {
	if (state->xfr) {
		cmsDeleteTransform(state->xfr);
	}
	if (state->tmp) {
		wuimg_free(state->tmp);
		free(state->tmp);
	}
	free(state->row);
	palette_unref(state->pal);
}

static float mix(const float a, const float b, const float k) {
	/* Linear interpolation. Equivalent to
	 *	a*(1 - k) + b*k
	*/
	return fmaf(b, k, fmaf(a, -k, a));
}

static float fractf(float val) {
	/* Returns the positive fractional part. This is different from
	 * fmod() and modf(), which return negative results for val < 0.
	 * This gets turned into a vroundss and vsubss instruction pair. */
	return val - floorf(val);
}

/* Faster fmax()/fmin() replacements. These get turned into vmaxss/vminss. */
static float min(float val, const float a) {
	return val < a ? val : a;
}

static float max(float val, const float a) {
	return val > a ? val : a;
}

static float clamp(float val, const float a, const float b) {
	val = min(val, b);
	val = max(val, a);
	return val;
}

static float saturate(float val) {
	return clamp(val, 0, 1);
}

/* Faster exp2f(), log2f(), and powf()
 * These are less precise than their libc counterparts and don't check for
 * special cases. When converting to uint16, they are off by one ~10% of the
 * time.
 * Polynomials were found using Sollya, which should produce more accurate
 * single-precision coefficients than simply truncating high-precision ones.
https://www.sollya.org/
*/
static float fastexp2f_unchecked(float x) {
	// Build a float equal to 2^(intpart - 127)
	// x is assumed not to overflow the exponent field
	int32_t i = (int32_t)floorf(x);
	i = (i + 127) << 23;
	float e;
	memcpy(&e, &i, sizeof(i));

	// Get the positive fractional part.
	const float f = fractf(x);

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
	return fmaf(fmaf(fmaf(fmaf(a4, f, a3), f, a2), f, a1), f*e, e);
}

static float fastpowlog2f(float x, float mul) {
	// Extract the exponent of x
	uint32_t u;
	memcpy(&u, &x, sizeof(u));
	int32_t i = (int32_t)(u >> 23);
	float e = (float)((i & 0xff) - 127);

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
	x = fmaf(fmaf(fmaf(fmaf(fmaf(a5, m, a4), m, a3), m, a2), m, a1), m, a0);
	return fmaf(x, fmaf(mul, m, -mul), e * mul);
}

static float myexp2f(float x) {
	if (USE_MATH_APPROX) {
		// Prevent overflowing the exponent (but not underflowing!)
		return fastexp2f_unchecked(min(x, 128));
	}
	return exp2f(x);
}

static float mypowf(float x, float e) {
	if (USE_MATH_APPROX) {
		// This doesn't handle negative x, not even for integer e
		return fastexp2f_unchecked(fastpowlog2f(x, e));
	}
	return powf(x, e);
}

static unsigned poor_round(float val, float scale) {
	/* Like lroundf(), but seems slightly faster
	 * On par with lrintf(), but doesn't depend on the float environment. */
	return (unsigned)fmaf(val, scale, .5f);
}

static float get_ch(const void *data, const int32_t i, const uint8_t depth) {
	switch (depth) {
	case 1: return ((uint8_t *)data)[i];
	case 2: return ((uint16_t *)data)[i];
	}
	return ((float *)data)[i];
}

static void pack_row(void *restrict t, const float *row, const size_t w,
const uint8_t channels, const bool high_depth) {
	const float scale = high_depth ? 0xffff : 0xff;
	for (size_t i = 0; i < w*channels; ++i) {
		unsigned ival = poor_round(saturate(row[i]), scale);
		if (high_depth) {
			((uint16_t *)t)[i] = (uint16_t)(ival);
		} else {
			((uint8_t *)t)[i] = (uint8_t)(ival);
		}
	}
}

static float oetf_srgb(float v) {
	if (USE_MATH_APPROX) {
		/* With a faster powf, branching doesn't pay for itself
		 * anymore, so we use a series of conditional moves
		 * and call powf unconditionally, with a copysign for the
		 * negative linear part. */
		bool gamma = v > 0.0031308f;
		float x = gamma ? 1.0f/2.4f : 1;
		float m = gamma ? 1.055f : 12.92f;
		float a = gamma ? -0.055f : 0;
		return copysignf(fmaf(mypowf(v, x), m, a), v);
	}
	return v > 0.0031308f
		? fmaf(powf(v, 1.0f/2.4f), 1.055f, -0.055f)
		: v * 12.92f;
}

static float eotf(float v, const struct color_transfer *t) {
	const float *arg = t->args;
	switch (t->fn) {
	case color_transfer_linear_gamma: break;
	case color_transfer_pq:
		v = mypowf(max(v, 0), arg[0]);
		float num = v - min(arg[1], v); // a.k.a. fdim()
		float den = fmaf(v, -arg[3], arg[2]);
		return mypowf(num/den, arg[4]);
	case color_transfer_hlg:
		return v > arg[0]
			? myexp2f(fmaf(v, arg[1], arg[2])) + arg[3]
			: v * v * arg[4];
	}
	// Input to the linear-gamma EOTF may be negative
	float h = fabsf(v);
	bool gamma = h > arg[0];
	float e = gamma ? arg[1] : arg[4];
	float l = gamma ? arg[2] : 0.0f;
	float p = gamma ? arg[3] : 1.0f;
	return copysignf(mypowf(fmaf(h, e, l), p), v);
}

static void convert_alpha(float *pix, const size_t w, const uint8_t ch,
const enum alpha_interpretation alpha, const bool high_depth) {
	if (ch != 2 && ch != 4) {
		return;
	}
	const float scale = high_depth ? 0xffff : 0xff;
	uint8_t a = ch - 1;
	for (size_t x = 0; x < w; ++x) {
		switch (alpha) {
		case alpha_associated:
			if (isnormal(pix[a])) {
				for (uint8_t z = 0; z < a; ++z) {
					pix[z] /= pix[a];
				}
			}
			// fallthrough
		case alpha_unassociated:
			pix[a] *= scale;
			break;
		case alpha_key:
			for (uint8_t z = 0; z < 3; ++z) {
				pix[z] *= pix[a];
			}
			// fallthrough
		case alpha_ignore:
			pix[a] = scale;
			break;
		}
		pix += ch;
	}
}

static bool convert_row_gray(void *restrict tgt, const float *restrict unpack,
size_t w, uint8_t channels, bool high_depth, enum alpha_interpretation alpha,
const struct imgconv *state) {
	if (!channels) {
		return false;
	}
	const struct color_convert *cs = &state->color;
	const bool has_alpha = channels > 1;
	float *t = tgt;
	for (size_t x = 0; x < w; ++x) {
		const float *orig = unpack + x*channels;
		if (has_alpha) {
			t[x*channels+1] = fmaf(orig[1], cs->map.mul[3], cs->map.add[3]);
		}
		t[x*channels] = fmaf(orig[0], cs->map.mul[0], cs->map.add[0]);
	}

	const bool process_alpha = has_alpha && alpha != alpha_unassociated;
	if (state->src->cs.type == color_profile_icc) {
		cmsDoTransform(state->xfr, t, t, (cmsUInt32Number)w);
		return false;
	} else if (!cs->eotf.srgb_input || process_alpha) {
		for (size_t x = 0; x < w; ++x) {
			float *pix = t + x*channels;
			*pix = eotf(*pix, &cs->eotf);
		}

		convert_alpha(t, w, channels, alpha, high_depth);

		for (size_t x = 0; x < w; ++x) {
			float *pix = t + x*channels;
			*pix = oetf_srgb(*pix);
		}
	}
	return true;
}

static bool convert_row_color(void *restrict tgt, const float *restrict unpack,
size_t w, uint8_t channels, bool high_depth, enum alpha_interpretation alpha_type,
const struct imgconv *state) {
	if (channels > 4) {
		return false;
	}
	const struct color_convert *cs = &state->color;
	const bool has_alpha = channels > 3;
	float *t = tgt;
	for (size_t x = 0; x < w; ++x) {
		const float *orig = unpack + x*channels;
		float tmp[4];
		memcpy(tmp, orig, sizeof(*tmp) * channels);
		tmp[3] = has_alpha ? orig[3] : 1;

		for (size_t z = 0; z < ARRAY_LEN(tmp); ++z) {
			tmp[z] = fmaf(tmp[z], cs->map.mul[z], cs->map.add[z]);
		}

		float *dst = t + x*channels;
		matf_mul(dst, tmp, cs->nonlinear.m, 3, 1, 3);
		memcpy(dst + 3, tmp + 3, sizeof(*tmp) * has_alpha);
	}

	const bool process_alpha = has_alpha && alpha_type != alpha_unassociated;
	if (state->src->cs.type == color_profile_icc) {
		cmsDoTransform(state->xfr, t, t, (cmsUInt32Number)w);
		return false;
	} else if (!cs->eotf.srgb_input || process_alpha) {
		for (size_t x = 0; x < w; ++x) {
			float *pix = t + x*channels;
			float tmp[3];
			for (uint8_t z = 0; z < ARRAY_LEN(tmp); ++z) {
				tmp[z] = eotf(pix[z], &cs->eotf);
			}
			matf_mul(pix, cs->linear.m, tmp, 3, 3, 1);
		}

		convert_alpha(t, w, channels, alpha_type, high_depth);

		for (size_t x = 0; x < w; ++x) {
			float *pix = t + x*channels;
			for (uint8_t z = 0; z < 3; ++z) {
				pix[z] = oetf_srgb(pix[z]);
			}
		}
	}
	return true;
}

static void convert_row(void *restrict tgt, const float *restrict unpack,
size_t w, uint8_t channels, uint8_t depth, const struct wuimg *src,
const struct imgconv *state) {
	const enum alpha_interpretation a = src->alpha;
	const bool high_depth = depth > 8;
	float *row = (float *)state->row;
	const bool pack = (channels >= 3)
		? convert_row_color(row, unpack, w, channels, high_depth, a, state)
		: convert_row_gray(row, unpack, w, channels, high_depth, a, state);
	if (pack) {
		pack_row(tgt, row, w, channels, high_depth);
	}
}

static void * process_row(void *restrict tgt, const void *restrict unpack,
ptrdiff_t pix_stride, size_t w, uint8_t channels, uint8_t bitdepth,
const struct wuimg *src, const struct imgconv *state) {
	/* Copy an unpacked and possibly rotated row into linear order.
	 * Swizzle into RGBA or GrayAlpha order while at it. */
	const uint8_t ud = state->unpack_depth/8;
	const uint8_t *u = unpack;

	uint8_t swz[4];
	pix_layout_map(swz, src->layout);

	// Expand to float while swizzling
	float *tmp = (float *)state->row;
	for (size_t x = 0; x < w; ++x) {
		for (uint8_t z = 0; z < channels; ++z) {
			tmp[x*channels + z] = get_ch(u, swz[z], ud);
		}
		u += pix_stride;
	}
	convert_row(tgt, tmp, w, channels, bitdepth, src, state);
	return tgt;
}

static bool mirror_swap(const struct wuimg *src) {
	return src->mirror ^ (bool)(src->rotate & 2);
}

struct planar_subsamp {
	float samp, off;
};

static void subsampled_plane(float *pix, const float *limit, const uint8_t ch,
const struct plane_info *p, int ix, int xadd, int iy, int yadd, uint8_t usize,
const struct planar_subsamp *xp, const struct planar_subsamp *yp) {
	// This function works perfectly. It's also slow as molasses. FIXME
	const uint8_t *ptr = p->ptr;
	const int stride = (int)p->stride;
	int ws = (int)(p->w - 1);
	int hs = (int)(p->h - 1);
	float fx = (float)ix;
	float fy = (float)iy;
	while (pix < limit) {
		float xs = fmaf(fx, xp->samp, xp->off);
		float ys = fmaf(fy, yp->samp, yp->off);
		float xm = fractf(xs);
		float ym = fractf(ys);
		int xlo = (int)floorf(xs);
		int ylo = (int)floorf(ys);
		int xhi = xlo + (xlo < ws);
		int yhi = ylo + (ylo < hs);
		xlo += xlo < 0;
		ylo += ylo < 0;
		*pix = mix(
			mix(
				get_ch(ptr + ylo*stride, xlo, usize),
				get_ch(ptr + ylo*stride, xhi, usize),
				xm),
			mix(
				get_ch(ptr + yhi*stride, xlo, usize),
				get_ch(ptr + yhi*stride, xhi, usize),
				xm),
			ym);
		pix += ch;
		fx += (float)xadd;
		fy += (float)yadd;
	}
}

static void full_plane(float *pix, const float *limit, const uint8_t ch,
const struct plane_info *p, int ix, int xadd, int iy, int yadd, uint8_t usize) {
	const uint8_t *ptr = p->ptr;
	const int stride = (int)p->stride;
	while (pix < limit) {
		*pix = get_ch(ptr + iy*stride, ix, usize);
		pix += ch;
		ix += xadd;
		iy += yadd;
	}
}

static void set_params(struct planar_subsamp *c, const struct plane_dim *d) {
	c->samp = 1.f/d->subsamp;
	c->off = (d->cosit || d->subsamp < 2)
		? 0 : -.5f + fractf(c->samp * c->samp);
}

static uint8_t * get_planar_row(const struct imgconv *state, size_t y) {
	/* Produce one row of interleaved output out of a planar and possibly
	 * rotated image, doing linear interpolation on subsampled planes. */
	const struct wuimg *src = state->tmp ? state->tmp : state->src;
	const struct wuimg *dst = state->dst;
	const struct plane_info *p = src->u.planes->p;

	const uint8_t channels = dst->channels;
	const uint8_t ud = state->unpack_depth/8;
	const bool quarter = src->rotate & 1;

	uint8_t swz[4];
	pix_layout_map(swz, src->layout);

	const size_t rowdims = dst->w * channels;
	float *tgt = (float *)(state->row + (state->row_len & ~3u)) - rowdims;
	for (uint8_t out_z = 0; out_z < channels; ++out_z) {
		uint8_t z = swz[out_z];
		int w = (int)(src->w - 1);
		int h = (int)(src->h - 1);
		int ix, iy;
		int xadd, yadd;
		bool swap = mirror_swap(src);
		struct planar_subsamp xp, yp;
		if (quarter) {
			ix = (src->rotate & 2) ? w - (int)y : (int)y;
			xadd = 0;
			iy = swap ? 0 : h;
			yadd = swap ? 1 : -1;
		} else {
			ix = (src->rotate & 2) ? w : 0;
			xadd = (src->rotate & 2) ? -1 : 1;
			iy = swap ? h - (int)y : (int)y;
			yadd = 0;
		}
		set_params(&xp, &p[z].x);
		set_params(&yp, &p[z].y);

		float *pix = tgt + out_z;
		const float *limit = tgt + rowdims;
		if (p[z].x.subsamp < 2 && p[z].y.subsamp < 2) {
			full_plane(pix, limit, channels, p + z,
				ix, xadd, iy, yadd, ud);
		} else {
			subsampled_plane(pix, limit, channels, p + z,
				ix, xadd, iy, yadd, ud, &xp, &yp);
		}
	}
	convert_row(state->row, tgt, dst->w, channels, dst->bitdepth, src,
		state);
	return state->row;
}

static void * expand_pal(uint32_t *tgt, const uint8_t *restrict unpack,
const ptrdiff_t stride, const struct palette *pal, const size_t w) {
	for (size_t i = 0; i < w; ++i) {
		memcpy(tgt + i, pal->color + *unpack, sizeof(*pal->color));
		unpack += stride;
	}
	return tgt;
}

static uint8_t * interleaved_convert(const void *restrict unpack,
const ptrdiff_t stride, const struct wuimg *src, const struct imgconv *state) {
	void *restrict tgt = state->row;
	const struct wuimg *dst = state->dst;
	if (state->pal) {
		return expand_pal(tgt, unpack, stride, state->pal, dst->w);
	}
	return process_row(tgt, unpack, stride, dst->w, dst->channels,
		dst->bitdepth, src, state);
}

static const uint8_t * get_unpacked(const struct imgconv *state,
const struct wuimg *src, const size_t y) {
	const uint8_t *s_row = src->data + wuimg_stride(src) * y;
	if (state->op == op_noop) {
		// Just return the original row
		return s_row;
	}

	/* Otherwise, convert whatever we have into 8- or 16-bit uints,
	 * or 32-bit floats. */
	const void *arg = (state->op == op_bitfield)
		? (void *)src->u.bitfield : &src->bitrange;
	const size_t elems = src->w * src->channels;
	const size_t unpack_len = unpack_stride(elems, src->bitdepth,
		src->attr, state->op, arg);

	uint8_t *u_row = state->row + state->row_len - unpack_len;
	unpack_strip(u_row, s_row, elems,
		src->bitdepth, src->attr, state->op, arg);
	return u_row;
}

static uint8_t * get_row(const struct imgconv *state, size_t y) {
	const struct wuimg *src = state->src;
	y = mirror_swap(src) ? src->h - 1 - y : y;
	const uint8_t *u_row = get_unpacked(state, src, y);

	// Set reading direction
	ptrdiff_t pix_size = state->unpack_depth/8 * state->unpack_ch;
	if (src->rotate & 2) {
		u_row += (src->w - 1) * (size_t)pix_size;
		pix_size = -pix_size;
	}
	return interleaved_convert(u_row, pix_size, src, state);
}

static uint8_t * get_rotated_row(const struct imgconv *state, size_t col) {
	// Get either the original or the recently unpacked image
	const struct wuimg *src = state->tmp ? state->tmp : state->src;

	size_t pix_size = state->unpack_depth/8 * src->channels;
	ptrdiff_t stride = (ptrdiff_t)wuimg_stride(src);
	const uint8_t *data = src->data;
	if (src->rotate & 2) {
		// 3/4 clockwise rotation.
		data += pix_size * (src->w - 1 - col);
		if (src->mirror) {
			data += stride * (ptrdiff_t)(src->h - 1);
			stride = -stride;
		}
	} else {
		// 1/4 clockwise rotation.
		data += pix_size * col;
		if (!src->mirror) {
			data += stride * (ptrdiff_t)(src->h - 1);
			stride = -stride;
		}
	}
	return interleaved_convert(data, stride, src, state);
}

uint8_t * imgconv_get_row(const struct imgconv *state, size_t y) {
	if (state->src->mode == image_mode_planar) {
		return get_planar_row(state, y);
	} else if (state->src->rotate & 1) {
		return get_rotated_row(state, y);
	}
	return get_row(state, y);
}

static void get_plane(uint8_t *restrict t, const uint8_t *restrict s,
const size_t t_stride, const size_t s_stride, const size_t elems,
const size_t h, const struct wuimg *src, const enum unpack_op op) {
	for (size_t y = 0; y < h; ++y) {
		unpack_strip(t + y*t_stride, s + y*s_stride,
			elems, src->bitdepth, src->attr, op, src->u.bitfield);
	}
}

static void get_tmp_img(struct imgconv *state) {
	struct wuimg *tmp = state->tmp;
	const struct wuimg *src = state->src;

	if (tmp->mode == image_mode_planar) {
		const struct plane_info *tp = tmp->u.planes->p;
		const struct plane_info *sp = src->u.planes->p;
		for (uint8_t z = 0; z < tmp->channels; ++z) {
			get_plane(tp[z].ptr, sp[z].ptr,
				tp[z].stride, sp[z].stride, sp[z].w,
				sp[z].h, src, state->op);
		}
	} else {
		get_plane(tmp->data, src->data,
			wuimg_stride(tmp), wuimg_stride(src),
			tmp->w * tmp->channels, tmp->h, src, state->op);
	}
}

static enum wu_error init_tmp_img(struct imgconv *state,
const struct wuimg *src) {
	struct wuimg *tmp = calloc(1, sizeof(*state->tmp));
	if (tmp) {
		state->tmp = tmp;
		tmp->w = src->w;
		tmp->h = src->h;
		tmp->channels = state->unpack_ch;
		tmp->bitdepth = state->unpack_depth;
		tmp->alpha = src->alpha;
		tmp->layout = src->layout;
		tmp->attr = (src->attr == pix_float) ? src->attr : pix_normal;
		tmp->rotate = src->rotate;
		tmp->mirror = src->mirror;
		if (src->mode == image_mode_planar) {
			struct image_planes *planes = wuimg_plane_init(tmp);
			if (!planes) {
				return wu_alloc_error;
			}
			for (uint8_t z = 0; z < tmp->channels; ++z) {
				planes->p[z].x = src->u.planes->p[z].x;
				planes->p[z].y = src->u.planes->p[z].y;
			}
		}
		const enum wu_error st = wuimg_alloc(tmp);
		if (st == wu_ok) {
			get_tmp_img(state);
		}
		return st;
	}
	return wu_alloc_error;
}

static enum wu_error init_color(struct imgconv *state, const struct wuimg *dst,
const struct wuimg *src, const double range) {
	const bool is_planar = src->mode == image_mode_planar;
	if (!color_space_to_linear_sRGB(&src->cs, &state->color,
	src->layout == pix_gray, is_planar, range)) {
		return wu_invalid_params;
	}
	if (src->cs.type == color_profile_icc) {
		cmsHPROFILE prof = cmsCreate_sRGBProfile();
		if (!prof) {
			return wu_alloc_error;
		}
		const cmsUInt32Number in_fmt = icc_fmt(dst->channels,
			4, src->alpha);
		const cmsUInt32Number out_fmt = icc_fmt_colorspace(
			dst->channels,
			dst->bitdepth/8,
			alpha_unassociated,
			dst->channels >= 3 ? PT_RGB : PT_GRAY);
		state->xfr = color_icc_transform(&src->cs, prof,
			in_fmt, out_fmt);
		cmsCloseProfile(prof);
		if (!state->xfr) {
			return wu_alloc_error;
		}
	}
	return wu_ok;
}

const char * imgconv_init(struct imgconv *state, const struct wuimg *dst,
const struct wuimg *src) {
	*state = (struct imgconv) {
		.src = src,
		.dst = dst,
		.op = op_noop,
		.unpack_depth = dst->bitdepth,
		.unpack_ch = dst->channels,
	};
	if (src->attr == pix_float && src->bitdepth < 32) {
		return "Conversion from half-precision floats not supported";
	}
	double range = dst->bitdepth > 8 ? 0xffff : 0xff;
	size_t row_elems = dst->w * dst->channels;
	switch (src->mode) {
	case image_mode_palette:
		row_elems = zumax(row_elems, sizeof(state->pal->color));
		state->unpack_ch = 1;
		state->op = src->bitdepth < dst->bitdepth ? op_unpack : op_noop;
		range = exp2(src->bitrange) - 1;

		// Create a color-corrected palette
		state->pal = palette_new();
		if (!state->pal) {
			return "Couldn't allocate temporary palette";
		}
		break;
	case image_mode_bitfield:
		state->op = op_bitfield;
		break;
	case image_mode_planar:
	case image_mode_raw:
		if (src->attr == pix_float) {
			state->unpack_depth = 32;
			state->op = src->bitdepth > 32 ? op_pack : op_noop;
			range = 1;
		} else if (src->bitdepth > dst->bitdepth) {
			state->op = op_pack;
			range = exp2(src->bitrange > 16 ? 16 : src->bitrange) - 1;
		} else {
			if (src->bitdepth < dst->bitdepth || src->attr != pix_normal) {
				state->op = op_unpack;
			}
			range = exp2(src->bitrange) - 1;
		}
	}

	range = 1.0/range;
	enum wu_error st = init_color(state, dst, src, range);
	if (st != wu_ok) {
		return wu_error_message(st);
	}

	if (src->rotate & 1 || src->mode == image_mode_planar) {
		if (state->op != op_noop) {
			// Create a temporal unpacked image
			st = init_tmp_img(state, src);
			if (st != wu_ok) {
				return wu_error_message(st);
			}
		}
	}

	/* Reserve memory for one row of temporary float data and output */
	state->row_len = row_elems * sizeof(float)
		+ dst->w * state->unpack_depth/8 * state->unpack_ch;
	state->row = malloc(state->row_len);
	if (state->row) {
		if (state->pal) {
			process_row(state->pal->color, src->u.palette->color,
				4, 1 << src->bitdepth, 4, 8, src, state);
		}
		return NULL;
	}
	return "Failed to allocate row memory";
}
