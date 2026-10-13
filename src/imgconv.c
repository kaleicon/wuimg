// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <stdlib.h>

#include <lcms2.h>

#include "misc/common.h"
#include "misc/fast_math.h"
#include "misc/math.h"
#include "imgconv.h"

/* Enable for somewhat faster exp2f(), log2f() and powf(), otherwise use
 * libc functions. */
static const bool USE_MATH_APPROX = true;
/* Use a upsampling function that is faster but harder to understand. */
static const bool USE_UPSAMP_FASTER = true;
/* If ICC setup fails, return an error code and refuse to convert the image,
 * otherwise pretend there's no profile and hope for the best. */
static const bool FAIL_ON_BAD_ICC = false;

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

static float myexp2f(float x) {
	if (USE_MATH_APPROX) {
		return fm_exp2f(x);
	}
	return exp2f(x);
}

static float mypowf_unchecked(float x, float e) {
	if (USE_MATH_APPROX) {
		return fm_powf_unchecked(x, e);
	}
	return powf(x, e);
}

static float mypowf(float x, float e) {
	if (USE_MATH_APPROX) {
		return fm_powf(x, e);
	}
	return powf(x, e);
}

static void matff_mul(float *restrict out, const float *restrict m1,
const float *restrict m2, const int len, const int h1, const int w2,
const float init) {
	/* Reimplementation of matf_mul(), but using fm_fmaf and accepting an
	 * initial value. Nice to have when not compiling with LTO, too. */
	for (int y = 0; y < h1; ++y) {
		for (int x = 0; x < w2; ++x) {
			float acc = fm_fmaf(m1[y*len], m2[x], init);
			for (int i = 1; i < len; ++i) {
				acc = fm_fmaf(m1[y*len + i], m2[x + i*w2], acc);
			}
			out[y*w2 + x] = acc;
		}
	}
}

static float get_lum_scale(const struct imgconv *state) {
	const struct color_convert *cc = &state->color;
	return cc->in_lum * cc->out_lum_inv;
}

static bool image_has_alpha(const struct imgconv *state) {
	return !(state->dst->channels & 1);
}

static const struct wuimg * image_src(const struct imgconv *state) {
	return state->tmp ? state->tmp : state->src;
}

static uint8_t unpack_comp_size(const struct imgconv *state) {
	return state->unpack_depth/8;
}

static bool quarter_turn(const struct imgconv *state) {
	return state->src->rotate & 1;
}

static float get_ch(const void *data, const int32_t i, const uint8_t depth) {
	switch (depth) {
	case 1: return ((uint8_t *)data)[i];
	case 2: return ((uint16_t *)data)[i];
	}
	return ((float *)data)[i];
}

static void put_ch(void *dst, const size_t i, const float val,
const bool high_depth) {
	if (high_depth) {
		((uint16_t *)dst)[i] = (uint16_t)val;
	} else {
		((uint8_t *)dst)[i] = (uint8_t)val;
	}
}

/* Use the simple power-law transfer function for encoding into sRGB instead of
 * the piecewise function. See raster/color.c for an explanation. */
static const bool USE_DISPLAY_SRGB = true;
static float oetf_srgb(float v) {
	if (USE_DISPLAY_SRGB) {
		const float gamma = (float)(1/COLOR_SRGB_DISPLAY_GAMMA);
		return copysignf(mypowf_unchecked(fabsf(v), gamma), v);
	}
	return v > 0.0031308f
		? fm_fmaf(mypowf_unchecked(v, 1.0f/2.4f), 1.055f, -0.055f)
		: v * 12.92f;
}

static float eotf(float v, const struct color_transfer *t) {
	const float *arg = t->args;
	switch (t->fn) {
	case color_transfer_linear_gamma:
		// Input to the linear-gamma EOTF may be negative
		;float h = fabsf(v);
		bool gamma = h > arg[0];
		float e = gamma ? arg[1] : arg[4];
		float l = gamma ? arg[2] : 0.0f;
		float p = gamma ? arg[3] : 1.0f;
		return copysignf(mypowf(fm_fmaf(h, e, l), p), v);
	case color_transfer_pq:
		v = mypowf_unchecked(fm_fmaxf(v, 0), arg[0]);
		float num = v - fm_fminf(arg[1], v);
		float den = fm_fmaf(v, arg[3], arg[2]);
		return mypowf(num/den, arg[4]);
	case color_transfer_hlg:
		break;
	}
	return v > arg[0]
		? myexp2f(fm_fmaf(v, arg[1], arg[2])) + arg[3]
		: v * v * arg[4];
}


static void * pack_row_fn(void *t, void *row, const size_t w,
const uint8_t ch, const uint8_t depth, const struct imgconv *state) {
	// Map and pack a float in range [0.0, 1.0] into u8 or u16
	(void)depth;
	const bool high_depth = state->dst->bitdepth > 8;
	const float mul = high_depth ? 0xffff : 0xff;
	const float *src = row;
	for (size_t i = 0; i < w*ch; ++i) {
		put_ch(t, i, fm_pre_roundf(src[i], mul), high_depth);
	}
	return t;
}

static void * pack_row_nomul_fn(void *t, void *row, const size_t w,
const uint8_t ch, const uint8_t depth, const struct imgconv *state) {
	// Convert float as-is to u8 or u16
	(void)depth;
	const bool high_depth = state->dst->bitdepth > 8;
	const float *src = row;
	for (size_t i = 0; i < w*ch; ++i) {
		put_ch(t, i, src[i] + .5f, high_depth);
	}
	return t;
}

static void * cms_row_fn(void *tgt, void *row, const size_t w,
uint8_t ch, uint8_t depth, const struct imgconv *state) {
	// ICC transform
	(void)ch; (void)depth;
	cmsDoTransform(state->xfr, row, tgt, (cmsUInt32Number)w);
	return tgt;
}

static void * oetf_row_fn(void *tgt, void *row, const size_t w,
const uint8_t ch, uint8_t depth, const struct imgconv *state) {
	// Encode linear into sRGB
	(void)tgt; (void)depth; (void)state;
	const uint8_t colors = ch >= 3 ? 3 : 1;
	for (size_t x = 0; x < w; ++x) {
		float *pix = (float *)row + x*ch;
		for (uint8_t z = 0; z < colors; ++z) {
			pix[z] = oetf_srgb(pix[z]);
		}
	}
	return row;
}

static void * luminance_fn(void *tgt, void *row, const size_t w,
const uint8_t ch, const uint8_t depth, const struct imgconv *state) {
	// Scale image luminance. We in linear space with no alpha.
	(void)tgt; (void)depth;
	const float lum = get_lum_scale(state);
	for (size_t i = 0; i < w*ch; ++i) {
		((float *)row)[i] *= lum;
	}
	return row;
}

static void * alpha_dissociate_fn(void *tgt, void *row, const size_t w,
const uint8_t ch, const uint8_t depth, const struct imgconv *state) {
	// Convert to unassociated alpha, scale luminance. We in linear space.
	(void)tgt; (void)depth;
	const enum alpha_interpretation alpha = state->src->alpha;
	const float lum = get_lum_scale(state);
	const uint8_t a = ch - 1;
	for (size_t x = 0; x < w; ++x) {
		float *pix = (float *)row + x*ch;
		float l = lum;
		switch (alpha) {
		case alpha_associated:
			if (isnormal(pix[a])) {
				l /= pix[a];
			}
			break;
		case alpha_unassociated:
			break;
		case alpha_key:
			l *= pix[a];
			// fallthrough
		case alpha_ignore:
			pix[a] = 1;
			break;
		}
		for (uint8_t z = 0; z < a; ++z) {
			pix[z] *= l;
		}
	}
	return row;
}

static void * eotf_row1_fn(void *tgt, void *row, const size_t w,
const uint8_t ch, const uint8_t depth, const struct imgconv *state) {
	// Convert grayscale into linear space
	(void)tgt; (void)depth;
	const struct color_convert *cc = &state->color;
	for (size_t x = 0; x < w; ++x) {
		float *pix = (float *)row + x*ch;
		*pix = eotf(*pix, &cc->eotf);
	}
	return row;
}

static void * eotf_and_mat_row3_fn(void *tgt, void *row, const size_t w,
const uint8_t ch, const uint8_t depth, const struct imgconv *state) {
	// Convert color data into linear space, then do matrix mul.
	(void)tgt; (void)depth;
	const struct color_convert *cc = &state->color;
	for (size_t x = 0; x < w; ++x) {
		float *pix = (float *)row + x*ch;
		float tmp[3];
		for (uint8_t z = 0; z < ARRAY_LEN(tmp); ++z) {
			tmp[z] = eotf(pix[z], &cc->eotf);
		}
		matff_mul(pix, tmp, cc->linear.m, 3, 1, 3, 0);
	}
	return row;
}

static void * remap_gray_fn(void *tgt, void *row, const size_t w,
const uint8_t ch, const uint8_t depth, const struct imgconv *state) {
	// Remap grayscale data in non-linear space.
	(void)tgt; (void)depth;
	const struct color_convert *cc = &state->color;
	const bool has_alpha = ch == 2;

	const bool inv_a = cc->eotf.invert_input;
	const float mul = cc->nonlinear.m[0];
	const float add = fmaf(cc->color_offset[0], mul, inv_a);
	float a_map[2];
	memcpy(a_map, cc->alpha_map, sizeof(a_map));
	for (size_t x = 0; x < w; ++x) {
		float *pix = (float *)row + x*ch;
		pix[0] = fm_fmaf(pix[0], mul, add);
		if (has_alpha) {
			pix[1] = fm_fmaf(pix[1], a_map[0], a_map[1]);
		}
	}
	return row;
}

static void * remap_color_fn(void *tgt, void *row, const size_t w,
const uint8_t ch, const uint8_t depth, const struct imgconv *state) {
	// Remap color data in non-linear space.
	(void)tgt; (void)depth;
	const struct color_convert *cc = &state->color;
	const bool has_alpha = ch == 4;
	const float inv_a = cc->eotf.invert_input;

	float off[3], a_map[2];
	memcpy(off, cc->color_offset, sizeof(off));
	memcpy(a_map, cc->alpha_map, sizeof(a_map));
	for (size_t x = 0; x < w; ++x) {
		float *pix = (float *)row + x*ch;
		float tmp[3];

		for (uint8_t z = 0; z < ARRAY_LEN(tmp); ++z) {
			tmp[z] = pix[z] + off[z];
		}
		matff_mul(pix, tmp, cc->nonlinear.m, 3, 1, 3, inv_a);
		if (has_alpha) {
			pix[3] = fm_fmaf(pix[3], a_map[0], a_map[1]);
		}
	}
	return row;
}

static void * expand_to_float_fn(void *tgt, void *unpack, const size_t w,
const uint8_t ch, const uint8_t bitdepth, const struct imgconv *state) {
	/* We're given pixels at some distance from each other, and components
	 * interleaved in some order. Expand to floats in RGBA or GrayAlpha
	 * order. */
	(void)tgt; (void)bitdepth;
	const ptrdiff_t pix_stride = state->pix_stride;
	const uint8_t ud = unpack_comp_size(state);
	const uint8_t *src = unpack;
	float *dst = (float *)state->row;
	for (size_t x = 0; x < w; ++x) {
		for (uint8_t z = 0; z < ch; ++z) {
			dst[x*ch + z] = get_ch(src, state->swz[z], ud);
		}
		src += pix_stride;
	}
	return dst;
}

static void * swizzle_row_fn(void *tgt, void *unpack, const size_t w,
const uint8_t ch, const uint8_t bitdepth, const struct imgconv *state) {
	// Read across the image and swizzle channel order
	(void)bitdepth;
	const ptrdiff_t pix_stride = state->pix_stride;
	const uint8_t comp_size = unpack_comp_size(state);
	const size_t pix_size = comp_size*ch;

	const uint8_t *src = unpack;
	uint8_t *dst = state->src->attr == pix_float ? state->row : tgt;
	for (size_t x = 0; x < w; ++x) {
		for (uint8_t z = 0; z < ch; ++z) {
			memcpy(dst + x*pix_size + z*comp_size,
				src + state->swz[z]*comp_size,
				comp_size);
		}
		src += pix_stride;
	}
	return dst;
}

static void * upright_fn(void *tgt, void *unpack, const size_t w,
const uint8_t ch, const uint8_t bitdepth, const struct imgconv *state) {
	// Read across the image
	(void)bitdepth;
	const ptrdiff_t pix_stride = state->pix_stride;
	const size_t pix_size = unpack_comp_size(state)*ch;

	const uint8_t *src = unpack;
	uint8_t *dst = state->src->attr == pix_float ? state->row : tgt;
	for (size_t x = 0; x < w; ++x) {
		memcpy(dst + x*pix_size, src, pix_size);
		src += pix_stride;
	}
	return dst;
}

static void * memcpy_fn(void *tgt, void *unpack, const size_t w,
const uint8_t ch, const uint8_t bitdepth, const struct imgconv *state) {
	(void)bitdepth;
	memcpy(tgt, unpack, w*ch*unpack_comp_size(state));
	return tgt;
}

static void * pal_convert_fn(void *tgt, void *unpack, const size_t w,
const uint8_t ch, const uint8_t bitdepth, const struct imgconv *state) {
	/* Palette is color-corrected during the initial setup, so copy values
	 * directly. */
	(void)ch; (void)bitdepth;
	const ptrdiff_t stride = state->pix_stride;
	const struct palette *pal = state->pal;
	const uint8_t *src = unpack;
	uint32_t *dst = tgt;
	for (size_t i = 0; i < w; ++i) {
		memcpy(dst + i, pal->color + *src, sizeof(*pal->color));
		src += stride;
	}
	return dst;
}

static void * imgconv_fn_run(void *tgt, void *row, const size_t w,
const uint8_t ch, const uint8_t depth, const struct imgconv *state) {
	for (unsigned i = 0; i < state->fn_len; ++i) {
		row = state->fn[i](tgt, row, w, ch, depth, state);
	}
	return tgt;
}


static uint8_t * interleaved_convert(void *restrict tgt, void *restrict unpack,
const struct imgconv *state) {
	const struct wuimg *dst = state->dst;
	return imgconv_fn_run(tgt, unpack, dst->w, dst->channels, dst->bitdepth,
		state);
}

static bool mirror_swap(const struct wuimg *src) {
	return src->mirror ^ (bool)(src->rotate & 2);
}

struct planar_subsamp {
	float samp, off;
	float init;
	float wrap;
	float chg;
};

static void set_params(struct planar_subsamp *c, const struct plane_dim *d,
const int add) {
	const uint8_t sub = d->subsamp;
	const bool match_grid = d->cosit | (sub == 1);
	c->samp = 1.f/sub;
	c->off = match_grid ? 0 : -.5f + fm_fractf(1.f/(sub*sub));
	c->init = add < 0 ? sub - 1 : 0;
	c->wrap = add < 0 ? 0 : sub - 1;
	c->chg = match_grid
		? c->wrap
		: (float)((sub + (add < 0 ? -1 : 1))/2);
}

static float spos(const float coord, const struct planar_subsamp *p) {
	return fm_fmaf(coord, p->samp, p->off);
}

static float mix_factor(const float coord, const struct planar_subsamp *p) {
	return fm_fractf(spos(coord, p));
}

static void upsamp_plane4(float *pix, const float *limit, const uint8_t ch,
const struct plane_info *p, int ix, int xadd, int iy, int yadd, uint8_t usize) {
	// Fast upsampling function
	const bool x = xadd;

	const ptrdiff_t stride = (ptrdiff_t)p->stride;
	const ptrdiff_t vstride = x ? usize : stride;
	const ptrdiff_t cstride = x ? stride : usize;
	const ptrdiff_t vlast = (ptrdiff_t)((x ? p->w : p->h) - 1) * vstride;
	const ptrdiff_t clast = (ptrdiff_t)((x ? p->h : p->w) - 1) * cstride;

	const int vadd = x ? xadd : yadd;
	const int cadd = 0;
	const ptrdiff_t limv = vadd >= 0 ? vlast : 0;
	const ptrdiff_t vdiff = vadd * vstride;

	const struct plane_dim *dpv = x ? &p->x : &p->y;
	const struct plane_dim *dpc = x ? &p->y : &p->x;

	struct planar_subsamp pv, pc;
	set_params(&pv, dpv, vadd);
	set_params(&pc, dpc, cadd);

	ptrdiff_t v = x ? ix : iy;
	ptrdiff_t c = x ? iy : ix;
	float fv = (float)(v % dpv->subsamp);
	const float fc = (float)(c % dpc->subsamp);
	const float mc = mix_factor(fc, &pc);

	v = (ptrdiff_t)floorf(spos((float)v, &pv)) * vstride;
	v += (vadd < 0) * vstride;
	c = (ptrdiff_t)floorf(spos((float)c, &pc)) * cstride;
	const ptrdiff_t loc = c + (c < 0) * cstride;
	const ptrdiff_t hic = c + (c < clast) * cstride;
	const uint8_t *ptr_lo = p->ptr + loc;
	const uint8_t *ptr_hi = p->ptr + hic;

	float g[2];
	const bool up = vadd >= 0;
	g[up] = fm_mix(
		get_ch(ptr_lo + v + vdiff, 0, usize),
		get_ch(ptr_hi + v + vdiff, 0, usize),
		mc);
	while (pix < limit) {
		v += (v != limv)*vdiff;
		g[!up] = g[up];
		g[up] = fm_mix(
			get_ch(ptr_lo + v, 0, usize),
			get_ch(ptr_hi + v, 0, usize),
			mc);
		do {
			const float mv = mix_factor(fv, &pv);
			*pix = fm_mix(g[0], g[1], mv);
			fv = (fv == pv.wrap) ? pv.init : fv + (float)vadd;
			pix += ch;
		} while (pix < limit && fv != pv.chg);
	}
}

static void upsamp_plane(float *pix, const float *limit, const uint8_t ch,
const struct plane_info *p, int ix, int xadd, int iy, int yadd, uint8_t usize) {
	// Reference upsampling function.
	struct planar_subsamp xp, yp;
	set_params(&xp, &p->x, xadd);
	set_params(&yp, &p->y, yadd);

	const uint8_t *ptr = p->ptr;
	const int stride = (int)p->stride;
	int ws = (int)(p->w - 1);
	int hs = (int)(p->h - 1);
	float fx = (float)ix;
	float fy = (float)iy;
	while (pix < limit) {
		float xs = spos(fx, &xp);
		float ys = spos(fy, &yp);
		float xm = fm_fractf(xs);
		float ym = fm_fractf(ys);
		int xlo = (int)floorf(xs);
		int ylo = (int)floorf(ys);
		int xhi = xlo + (xlo < ws);
		int yhi = ylo + (ylo < hs);
		xlo += xlo < 0;
		ylo += ylo < 0;
		*pix = fm_mix(
			fm_mix(
				get_ch(ptr + ylo*stride, xlo, usize),
				get_ch(ptr + ylo*stride, xhi, usize),
				xm),
			fm_mix(
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

static uint8_t * planar_convert(const struct imgconv *state, size_t y,
void *restrict tgt) {
	/* Produce one row of interleaved output out of a planar and possibly
	 * rotated image, doing linear interpolation on subsampled planes. */
	const struct wuimg *src = image_src(state);
	const struct wuimg *dst = state->dst;
	const struct plane_info *p = src->u.planes->p;

	const uint8_t channels = src->channels;
	const uint8_t ud = unpack_comp_size(state);
	const bool quarter = quarter_turn(state);

	int w = (int)(src->w - 1);
	int h = (int)(src->h - 1);
	int ix, iy;
	int xadd, yadd;
	const bool swap = mirror_swap(src);
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

	const size_t rowlen = dst->w * channels;
	float *row = (float *)state->row;
	const float *limit = row + rowlen;
	for (uint8_t out_z = 0; out_z < channels; ++out_z) {
		const uint8_t z = state->swz[out_z];
		float *pix = row + out_z;
		if (p[z].x.subsamp <= 1 && p[z].y.subsamp <= 1) {
			full_plane(pix, limit, channels, p+z, ix, xadd, iy,
				yadd, ud);
		} else {
			(USE_UPSAMP_FASTER ? upsamp_plane4 : upsamp_plane)
				(pix, limit, channels, p+z, ix, xadd, iy, yadd, ud);
		}
	}
	return interleaved_convert(tgt, row, state);
}

static uint8_t * get_unpacked(const struct imgconv *state,
const struct wuimg *src, const size_t y) {
	uint8_t *s_row = src->data + wuimg_stride(src) * y;
	if (!state->unpack_len) {
		// Nothing needs to be done, return pointer to original row
		return s_row;
	}

	/* Otherwise, convert whatever we have into 8- or 16-bit uints,
	 * or 32-bit floats. */
	const void *arg = (state->op == op_bitfield)
		? (void *)src->u.bitfield : &src->bitrange;
	const size_t elems = src->w * src->channels;
	uint8_t *u_row = state->row + state->row_len - state->unpack_len;
	unpack_strip(u_row, s_row, elems,
		src->bitdepth, src->attr, src->bit, state->op, arg);
	return u_row;
}

static uint8_t * get_row(const struct imgconv *state, size_t y,
void *restrict tgt) {
	/* Image may have half-rotations and may be mirrored. It's not
	 * planar, but may require unpacking before accessing pixels. */
	const struct wuimg *src = image_src(state);
	y = mirror_swap(src) ? src->h - 1 - y : y;
	uint8_t *u_row = get_unpacked(state, src, y);

	if (src->rotate & 2) {
		u_row += (src->w - 1) * (size_t)-state->pix_stride;
	}
	return interleaved_convert(tgt, u_row, state);
}

static uint8_t * get_rotated_row(const struct imgconv *state, size_t col,
void *restrict tgt) {
	/* Image has a quarter rotation and may be mirrored. It's not planar,
	 * and any required unpacking was done during initialization. */

	const struct wuimg *src = image_src(state);
	const ptrdiff_t stride = state->pix_stride;
	const size_t pix_size = unpack_comp_size(state) * src->channels;
	uint8_t *data = src->data;
	if (src->rotate & 2) {
		// 3/4 clockwise rotation.
		data += pix_size * (src->w - 1 - col);
		if (src->mirror) {
			data += -stride * (ptrdiff_t)(src->h - 1);
		}
	} else {
		// 1/4 clockwise rotation.
		data += pix_size * col;
		if (!src->mirror) {
			data += -stride * (ptrdiff_t)(src->h - 1);
		}
	}
	return interleaved_convert(tgt, data, state);
}

uint8_t * imgconv_get_row(const struct imgconv *state, size_t y,
void *restrict tgt) {
	if (state->src->mode == image_mode_planar) {
		return planar_convert(state, y, tgt);
	} else if (quarter_turn(state)) {
		return get_rotated_row(state, y, tgt);
	}
	return get_row(state, y, tgt);
}

static void data_unpack(uint8_t *restrict t, const uint8_t *restrict s,
const size_t t_stride, const size_t s_stride, const size_t elems,
const size_t h, const struct wuimg *src, const enum unpack_op op) {
	const void *arg = op == op_bitfield
		? (void *)src->u.bitfield : &src->bitrange;
	for (size_t y = 0; y < h; ++y) {
		unpack_strip(t + y*t_stride, s + y*s_stride,
			elems, src->bitdepth, src->attr, src->bit, op, arg);
	}
}

static void get_tmp_img(struct wuimg *restrict tmp,
const struct wuimg *restrict src, struct imgconv *state) {
	if (tmp->mode == image_mode_planar) {
		const struct plane_info *tp = tmp->u.planes->p;
		const struct plane_info *sp = src->u.planes->p;
		for (uint8_t z = 0; z < tmp->channels; ++z) {
			data_unpack(tp[z].ptr, sp[z].ptr,
				tp[z].stride, sp[z].stride, sp[z].w,
				sp[z].h, src, state->op);
		}
	} else {
		size_t elems = tmp->w * tmp->channels;
		size_t h = tmp->h;
		size_t t_stride, s_stride;
		if (src->align_sh <= 0) {
			elems *= tmp->h;
			h = 1;
			t_stride = wuimg_size(tmp);
			s_stride = wuimg_size(src);
		} else {
			t_stride = wuimg_stride(tmp);
			s_stride = wuimg_stride(src);
		}
		data_unpack(tmp->data, src->data, t_stride, s_stride,
			elems, h, src, state->op);
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
			get_tmp_img(tmp, src, state);
		}
		return st;
	}
	return wu_alloc_error;
}

static bool needs_alpha_diss(const struct wuimg *src) {
	switch (src->alpha) {
	case alpha_associated: case alpha_key:
		return true;
	case alpha_ignore: case alpha_unassociated:
		break;
	}
	return false;
}

static enum wu_error init_color(struct imgconv *state, const struct wuimg *dst,
const struct wuimg *src, const double range) {
	const bool is_planar = src->mode == image_mode_planar;
	color_space_to_sRGB(&src->cs, &state->color, src->layout == pix_gray,
		is_planar, range);
	if (src->cs.type == color_profile_icc) {
		cmsHPROFILE prof = cmsCreate_sRGBProfile();
		if (!prof) {
			return wu_alloc_error;
		}
		const uint32_t in_fmt = icc_fmt(dst->channels, sizeof(float),
			src->alpha);
		const uint32_t out_fmt = icc_fmt_colorspace(
			dst->channels,
			dst->bitdepth/8,
			dst->alpha,
			dst->channels >= 3 ? PT_RGB : PT_GRAY);
		state->xfr = color_icc_transform(&src->cs, prof,
			in_fmt, out_fmt);
		cmsCloseProfile(prof);
		if (!state->xfr) {
			if (FAIL_ON_BAD_ICC) {
				return wu_invalid_params;
			}
			fputs("Failed to setup ICC transform, will go on...\n",
				stderr);
		}
	}
	return wu_ok;
}

static void put_fn(struct imgconv *state, const imgconv_fn fn) {
	state->fn[state->fn_len] = fn;
	++state->fn_len;
}

static void put_fn_if(struct imgconv *state, const imgconv_fn fn,
const bool cond) {
	if (cond) {
		put_fn(state, fn);
	}
}

const char * imgconv_init(struct imgconv *state, const struct wuimg *dst,
const struct wuimg *src) {
	if (src->attr == pix_float && src->bitdepth < 32) {
		return "Conversion from half-precision floats not yet supported";
	} else if (src->channels > 4) {
		return "Conversion from channels > 4 not supported";
	}

	*state = (struct imgconv) {
		.src = src,
		.dst = dst,
		.op = op_noop,
		.unpack_depth = dst->bitdepth,
		.unpack_ch = dst->channels,
	};
	pix_layout_min_map(state->swz, src->layout);

	/* Colorspace operations require that we convert image components to
	 * floats. We can do that directly when the original uses 8-bit ints,
	 * 16-bit ints, or 32-bit floats.
	 * If the image is not in any of those formats, set parameters to
	 * `unpack_strip()` so we can call it before operating on a row. */
	const double outrange = dst->bitdepth > 8 ? 0xffff : 0xff;
	double inrange = exp2(src->bitrange) - 1;
	size_t row_elems = dst->w * dst->channels;
	switch (src->mode) {
	case image_mode_palette:
		row_elems = zumax(row_elems, sizeof(state->pal->color));
		state->unpack_ch = 1;
		state->op = src->bitdepth < dst->bitdepth ? op_repack : op_noop;
		break;
	case image_mode_bitfield:
		state->op = op_bitfield;
		inrange = outrange;
		break;
	case image_mode_planar:
	case image_mode_raw:
		if (src->attr == pix_float) {
			state->unpack_depth = 32;
			state->op = src->bitdepth > 32 ? op_repack : op_noop;
			inrange = 1;
		} else if (src->bitdepth > dst->bitdepth) {
			state->op = op_repack;
			inrange = fmin(inrange, 0xffff);
		} else if (src->bitdepth < dst->bitdepth || src->attr != pix_normal) {
			state->op = op_repack;
		}
	}

	enum wu_error st = init_color(state, dst, src, 1.0/inrange);
	if (st != wu_ok) {
		return wu_error_str(st);
	}

	const bool skip_linear = !(state->color.steps & color_step_linear)
		&& state->color.eotf.srgb && !needs_alpha_diss(src);
	enum color_steps omit = (!state->xfr ? color_step_icc : 0u)
		| (skip_linear ? (color_step_eotf | color_step_oetf) : 0u);
	state->color.steps &= ~omit;

	const bool skip_normal = state->color.steps == color_step_normalize
		&& inrange == outrange
		&& src->bitrange == dst->bitrange
		&& src->alpha == alpha_unassociated;
	state->color.steps &= ~(skip_normal ? color_step_normalize : 0u);

	if (getenv("WU_DEBUG")) {
		const enum color_steps steps = state->color.steps;
		fprintf(stderr, "Color steps:\n"
			" normalize: %d, nonlinear:%d eotf:%d linear:%d oetf:%d icc:%d\n"
			" in-range: %f, out-range: %f\n",
			!!(steps & color_step_normalize),
			!!(steps & color_step_nonlinear),
			!!(steps & color_step_eotf),
			!!(steps & color_step_linear),
			!!(steps & color_step_oetf),
			!!(steps & color_step_icc),
			inrange, outrange);
	}

	if (state->op != op_noop) {
		/* If we can't read pixels directly, and we can't access rows
		 * independently either, then we must unpack the whole image
		 * and refer to the copy instead of the original. */
		if (quarter_turn(state) || src->mode == image_mode_planar
		|| src->align_sh == align_bitpack) {
			st = init_tmp_img(state, src);
			if (st != wu_ok) {
				return wu_error_str(st);
			}
		} else {
			const void *arg = (state->op == op_bitfield)
				? (void *)src->u.bitfield : &src->bitrange;
			state->unpack_len = unpack_stride(src->w*src->channels,
				src->bitdepth, src->attr, state->op, arg);
		}
	}

	// Set reading direction
	if (src->mode == image_mode_planar) {
		state->pix_stride = sizeof(float)*dst->channels;
	} else if (quarter_turn(state)) {
		ptrdiff_t d = !((bool)(src->rotate & 2) ^ src->mirror) ? -1 : 1;
		state->pix_stride = (ptrdiff_t)wuimg_stride(image_src(state)) * d;
	} else {
		state->pix_stride = unpack_comp_size(state) * state->unpack_ch
			* ((src->rotate & 2) ? -1 : 1);
	}

	// Setup row functions
	if (state->color.steps) {
		put_fn_if(state, expand_to_float_fn,
			src->mode != image_mode_planar);

		const bool color = dst->channels >= 3;
		const bool remap = state->color.steps
			& (color_step_normalize | color_step_nonlinear);
		put_fn_if(state,
			(color ? remap_color_fn : remap_gray_fn), remap);
		if ((state->color.steps & color_step_icc)) {
			put_fn(state, cms_row_fn);
		} else {
			if (color) {
				const enum color_steps linear =
					color_step_eotf | color_step_linear;
				put_fn_if(state, eotf_and_mat_row3_fn,
					state->color.steps & linear);
			} else {
				put_fn_if(state, eotf_row1_fn,
					state->color.steps & color_step_eotf);
			}
			if (image_has_alpha(state)) {
				put_fn(state, alpha_dissociate_fn);
			} else if (get_lum_scale(state) != 1.0f) {
				put_fn(state, luminance_fn);
			}
			put_fn_if(state, oetf_row_fn,
				state->color.steps & color_step_oetf);
			put_fn(state, pack_row_fn);
		}
	} else if (src->mode == image_mode_planar) {
		put_fn(state, pack_row_nomul_fn);
	} else {
		if (src->layout != dst->layout) {
			put_fn(state, swizzle_row_fn);
		} else if (state->pix_stride != unpack_comp_size(state)*dst->channels) {
			put_fn(state, upright_fn);
		} else if (src->attr != pix_float) {
			put_fn(state, memcpy_fn);
		}
		put_fn_if(state, pack_row_fn, src->attr == pix_float);
	}

	/* Allocate row buffer. Buffer is used for
	 * - row unpacking (u8, u16, or float), placed at the end
	 * - colorspace conversion (float), placed at the beginning
	 * - packed output (u8 or u16), placed at the beginning
	 * In case the unpacked data takes as much memory as color corrected
	 * data, it must be offset by a pixel for swizzling to work correctly.
	 * Other than that, stages may overlap with no issues.
	 * FIXME: Don't allocate when none of these steps are neccesary. */
	state->row_len = row_elems + dst->channels;
	if (state->row_len >= SIZE_MAX/sizeof(float)) {
		return "Overflow on row memory size";
	}
	state->row_len *= sizeof(float);
	state->row = malloc(state->row_len);
	if (!state->row) {
		return "Failed to allocate row memory";
	}

	if (src->mode == image_mode_palette) {
		if (!state->color.steps && src->layout == dst->layout) {
			state->pal = palette_ref(src->u.palette);
		} else {
			// Create a color-corrected palette
			state->pal = palette_new();
			if (!state->pal) {
				return "Couldn't allocate temporary palette";
			}
			const ptrdiff_t ps = state->pix_stride;
			state->pix_stride = sizeof(*state->pal->color);
			imgconv_fn_run(state->pal->color, src->u.palette->color,
				1 << src->bitdepth, 4, 8, state);
			state->pix_stride = ps;
			state->fn_len = 0;
			put_fn(state, pal_convert_fn);
		}
	}
	return NULL;
}
