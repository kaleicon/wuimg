// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "misc/common.h"
#include "misc/math.h"
#include "imgconv.h"

#include "fast_math.c"

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

static float mypowf(float x, float e) {
	if (USE_MATH_APPROX) {
		return fm_powf(x, e);
	}
	return powf(x, e);
}

static void matff_mul(float *restrict out, const float *restrict m1,
const float *restrict m2, const int len, const int h1, const int w2) {
	/* Reimplementation of matf_mul(), but using fm_fmaf. Nice to have when
	 * not compiling with LTO, too. */
	for (int y = 0; y < h1; ++y) {
		for (int x = 0; x < w2; ++x) {
			float acc = m1[y*len] * m2[x];
			for (int i = 1; i < len; ++i) {
				acc = fm_fmaf(m1[y*len + i], m2[x + i*w2], acc);
			}
			out[y*w2 + x] = acc;
		}
	}
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

static void * pack_row(void *t, const float *row, const size_t w,
const uint8_t channels, const bool high_depth) {
	const float mul = high_depth ? 0xffff : 0xff;
	for (size_t i = 0; i < w*channels; ++i) {
		put_ch(t, i, fm_pre_roundf(row[i], mul), high_depth);
	}
	return t;
}

static void * pack_row_nomul(void *t, const float *row, const size_t w,
const uint8_t channels, const bool high_depth) {
	for (size_t i = 0; i < w*channels; ++i) {
		put_ch(t, i, row[i] + .5f, high_depth);
	}
	return t;
}

/* Use the simple power-law transfer function for encoding into sRGB instead of
 * the piecewise function. See raster/color.c for an explanation. */
static const bool USE_DISPLAY_SRGB = true;
static float oetf_srgb(float v) {
	if (USE_DISPLAY_SRGB) {
		const float gamma = (float)(1/COLOR_SRGB_DISPLAY_GAMMA);
		return copysignf(mypowf(fabsf(v), gamma), v);
	}
	return v > 0.0031308f
		? fm_fmaf(mypowf(v, 1.0f/2.4f), 1.055f, -0.055f)
		: v * 12.92f;
}

static float eotf(float v, const struct color_transfer *t) {
	const float *arg = t->args;
	switch (t->fn) {
	case color_transfer_linear_gamma: break;
	case color_transfer_pq:
		v = mypowf(fm_fmaxf(v, 0), arg[4]);
		float num = v - fm_fminf(arg[1], v);
		float den = fm_fmaf(v, -arg[3], arg[2]);
		return mypowf(num/den, arg[0]);
	case color_transfer_hlg:
		return v > arg[0]
			? myexp2f(fm_fmaf(v, arg[1], arg[2])) + arg[3]
			: v * v * arg[4];
	}
	// Input to the linear-gamma EOTF may be negative
	float h = fabsf(v);
	bool gamma = h > arg[0];
	float e = gamma ? arg[1] : arg[4];
	float l = gamma ? arg[2] : 0.0f;
	float p = gamma ? arg[3] : 1.0f;
	return copysignf(mypowf(fm_fmaf(h, e, l), p), v);
}

static void convert_alpha(float *row, const size_t w, const uint8_t ch,
const enum alpha_interpretation alpha) {
	if (ch != 2 && ch != 4) {
		return;
	}
	uint8_t a = ch - 1;
	for (size_t x = 0; x < w; ++x) {
		float *pix = row + x*ch;
		switch (alpha) {
		case alpha_associated:
			if (isnormal(pix[a])) {
				for (uint8_t z = 0; z < a; ++z) {
					pix[z] /= pix[a];
				}
			}
			break;
		case alpha_unassociated:
			break;
		case alpha_key:
			for (uint8_t z = 0; z < 3; ++z) {
				pix[z] *= pix[a];
			}
			// fallthrough
		case alpha_ignore:
			pix[a] = 1;
			break;
		}
	}
}

static float scale(const float x, const struct color_convert *cc,
const uint8_t i) {
	return fm_fmaf(x, cc->map.mul[i], cc->map.add[i]);
}

static bool convert_row_gray(float *row, size_t w, uint8_t channels,
const enum alpha_interpretation alpha, const struct imgconv *state) {
	const struct color_convert *cc = &state->color;
	const bool has_alpha = channels > 1;
	if ((cc->steps & color_step_map)) {
		for (size_t x = 0; x < w; ++x) {
			float *pix = row + x*channels;
			pix[0] = scale(pix[0], cc, 0);
			if (has_alpha) {
				pix[1] = scale(pix[1], cc, 3);
			}
		}
	}

	if ((cc->steps & (color_step_icc))) {
		return true;
	}

	const bool transfer = state->transfer;
	if (transfer && (cc->steps & color_step_eotf)) {
		for (size_t x = 0; x < w; ++x) {
			float *pix = row + x*channels;
			*pix = eotf(*pix, &cc->eotf);
		}
	}
	convert_alpha(row, w, channels, alpha);
	if (transfer) {
		for (size_t x = 0; x < w; ++x) {
			float *pix = row + x*channels;
			*pix = oetf_srgb(*pix);
		}
	}
	return false;
}

static bool convert_row_color(float *row, size_t w, uint8_t channels,
enum alpha_interpretation alpha, const struct imgconv *state) {
	const struct color_convert *cc = &state->color;
	const bool has_alpha = channels > 3;
	if ((cc->steps & (color_step_map | color_step_nonlinear))) {
		for (size_t x = 0; x < w; ++x) {
			float *pix = row + x*channels;
			float tmp[4];
			memcpy(tmp, pix, sizeof(*tmp) * channels);
			tmp[3] = has_alpha ? pix[3] : 1;

			for (uint8_t z = 0; z < ARRAY_LEN(tmp); ++z) {
				tmp[z] = scale(tmp[z], cc, z);
			}

			matff_mul(pix, tmp, cc->nonlinear.m, 3, 1, 3);
			memcpy(pix + 3, tmp + 3, sizeof(*tmp) * has_alpha);
		}
	}

	if ((cc->steps & (color_step_icc))) {
		return true;
	}

	const bool transfer = state->transfer;
	if (transfer && (cc->steps & (color_step_eotf | color_step_linear))) {
		for (size_t x = 0; x < w; ++x) {
			float *pix = row + x*channels;
			float tmp[3];
			for (uint8_t z = 0; z < ARRAY_LEN(tmp); ++z) {
				tmp[z] = eotf(pix[z], &cc->eotf);
			}
			matff_mul(pix, cc->linear.m, tmp, 3, 3, 1);
		}
	}
	convert_alpha(row, w, channels, alpha);
	if (transfer) {
		for (size_t x = 0; x < w; ++x) {
			float *pix = row + x*channels;
			for (uint8_t z = 0; z < 3; ++z) {
				pix[z] = oetf_srgb(pix[z]);
			}
		}
	}
	return false;
}

static void * convert_row(void *restrict tgt, float *restrict row,
size_t w, uint8_t channels, uint8_t depth, const struct wuimg *src,
const struct imgconv *state) {
	/* Perform color correction on a row of floats.
	 * There's an important assumption from here on: that channels < 3
	 * are grayscale, >= 3 are color, and that 2 and 4 include an Alpha
	 * channel. Just stating 'cause this might come to haunt us someday. */
	const bool icc = (channels >= 3 ? convert_row_color : convert_row_gray)
		(row, w, channels, src->alpha, state);
	if (icc) {
		cmsDoTransform(state->xfr, row, tgt, (cmsUInt32Number)w);
		return tgt;
	}
	return pack_row(tgt, row, w, channels, depth > 8);
}

static void * expand_row(void *restrict tgt, const void *restrict unpack,
ptrdiff_t pix_stride, size_t w, uint8_t channels, uint8_t bitdepth,
const struct wuimg *src, const struct imgconv *state) {
	/* We're given pixels at some distance from each other, and components
	 * interleaved in some order. Expand to floats in RGBA or GrayAlpha
	 * order. */
	const uint8_t ud = state->unpack_depth/8;
	const uint8_t *u = unpack;
	float *row = (float *)state->row;
	for (size_t x = 0; x < w; ++x) {
		for (uint8_t z = 0; z < channels; ++z) {
			row[x*channels + z] = get_ch(u, state->swz[z], ud);
		}
		u += pix_stride;
	}
	return convert_row(tgt, row, w, channels, bitdepth, src, state);
}

static void * raw_convert(void *restrict tgt, void *restrict unpack,
ptrdiff_t pix_stride, const struct wuimg *restrict dst,
const struct wuimg *restrict src, const struct imgconv *state) {
	const size_t w = dst->w;
	const uint8_t channels = dst->channels;
	const uint8_t bitdepth = dst->bitdepth;
	uint8_t *u = unpack;
	if (state->color_passthrough) {
		// Input/unpacked data matches output colorspace
		const uint8_t comp_size = state->unpack_depth/8;
		const size_t pix_size = comp_size*channels;
		uint8_t *t = src->attr == pix_float ? state->row : tgt;
		if (src->layout != state->dst->layout) {
			// Only swizzling is needed
			for (size_t x = 0; x < w; ++x) {
				for (uint8_t z = 0; z < channels; ++z) {
					memcpy(t + x*pix_size + z*comp_size,
						u + state->swz[z]*comp_size,
						comp_size);
				}
				u += pix_stride;
			}
		} else if ((ptrdiff_t)pix_size != pix_stride) {
			// Read across the image
			for (size_t x = 0; x < w; ++x) {
				memcpy(t + x*pix_size, u, pix_size);
				u += pix_stride;
			}
		} else if (src->attr == pix_float) {
			// Input is floating-point. Send to pack_row()
			t = u;
		} else {
			/* Input is linear and a pointer to the source image.
			 * Copy to output. */
			memcpy(t, u, pix_size*w);
		}
		if (src->attr == pix_float) {
			return pack_row(tgt, (float *)t, w, channels, bitdepth > 8);
		}
		return tgt;
	}
	return expand_row(tgt, unpack, pix_stride, w, channels, bitdepth,
		src, state);
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

static void upsamp_plane4(float *pix, const float *limit, const uint8_t ch,
const struct plane_info *p, int ix, int xadd, int iy, int yadd, uint8_t usize) {
	// Fast upsampling function
	const bool x = xadd;

	const ptrdiff_t stride = (ptrdiff_t)p->stride;
	const ptrdiff_t vstride = x ? usize : stride;
	const ptrdiff_t cstride = x ? stride : usize;
	const ptrdiff_t dv = (ptrdiff_t)((x ? p->w : p->h) - 1) * vstride;
	const ptrdiff_t dc = (ptrdiff_t)((x ? p->h : p->w) - 1) * cstride;

	ptrdiff_t v = x ? ix : iy;
	ptrdiff_t c = x ? iy : ix;
	const int vadd = x ? xadd : yadd;
	const int cadd = 0;
	const struct plane_dim *dpv = x ? &p->x : &p->y;
	const struct plane_dim *dpc = x ? &p->y : &p->x;

	struct planar_subsamp pv, pc;
	set_params(&pv, dpv, vadd);
	set_params(&pc, dpc, cadd);
	float fv = (float)(v % dpv->subsamp);
	float fc = (float)(c % dpc->subsamp);
	const float mc = fm_fractf(spos(fc, &pc));

	v = (ptrdiff_t)floorf(spos((float)v, &pv)) * vstride;
	v += (vadd < 0) * vstride;
	c = (ptrdiff_t)floorf(spos((float)c, &pc)) * cstride;
	const ptrdiff_t loc = c + (c < 0) * cstride;
	const ptrdiff_t hic = c + (c < dc) * cstride;

	const ptrdiff_t limv = vadd >= 0 ? dv : 0;
	const ptrdiff_t vdiff = vadd * vstride;
	ptrdiff_t vv = v + vdiff;

	const uint8_t *ptr = p->ptr;
	float g[2];
	const bool up = vadd >= 0;
	g[up] = fm_mix(
		get_ch(ptr + loc + vv, 0, usize),
		get_ch(ptr + hic + vv, 0, usize),
		mc);
	while (pix < limit) {
		v += (v != limv)*vdiff;
		vv = v;
		g[!up] = g[up];
		g[up] = fm_mix(
			get_ch(ptr + loc + vv, 0, usize),
			get_ch(ptr + hic + vv, 0, usize),
			mc);
		do {
			float mv = fm_fractf(spos(fv, &pv));
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
	const struct wuimg *src = state->tmp ? state->tmp : state->src;
	const struct wuimg *dst = state->dst;
	const struct plane_info *p = src->u.planes->p;

	const uint8_t channels = src->channels;
	const uint8_t ud = state->unpack_depth/8;
	const bool quarter = src->rotate & 1;

	int w = (int)(src->w - 1);
	int h = (int)(src->h - 1);
	int ix, iy;
	int xadd, yadd;
	bool swap = mirror_swap(src);
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
	return (state->color_passthrough)
		? pack_row_nomul(tgt, row, dst->w, channels, dst->bitdepth > 8)
		: convert_row(tgt, row, dst->w, channels, dst->bitdepth,
			src, state);
}

static void * pal_convert(uint32_t *tgt, const uint8_t *restrict unpack,
const ptrdiff_t stride, const struct palette *pal, const size_t w) {
	/* Palette is color-corrected during the initial setup, so copy values
	 * directly. */
	for (size_t i = 0; i < w; ++i) {
		memcpy(tgt + i, pal->color + *unpack, sizeof(*pal->color));
		unpack += stride;
	}
	return tgt;
}

static uint8_t * interleaved_convert(void *restrict tgt, void *restrict unpack,
const ptrdiff_t stride, const struct wuimg *src, const struct imgconv *state) {
	const struct wuimg *dst = state->dst;
	if (state->pal) {
		return pal_convert(tgt, unpack, stride, state->pal, dst->w);
	}
	return raw_convert(tgt, unpack, stride, dst, src, state);
}

static uint8_t * get_unpacked(const struct imgconv *state,
const struct wuimg *src, const size_t y) {
	uint8_t *s_row = src->data + wuimg_stride(src) * y;
	if (state->op == op_noop) {
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
	const struct wuimg *src = state->src;
	y = mirror_swap(src) ? src->h - 1 - y : y;
	uint8_t *u_row = get_unpacked(state, src, y);

	// Set reading direction
	ptrdiff_t pix_size = state->unpack_depth/8 * state->unpack_ch;
	if (src->rotate & 2) {
		u_row += (src->w - 1) * (size_t)pix_size;
		pix_size = -pix_size;
	}
	return interleaved_convert(tgt, u_row, pix_size, src, state);
}

static uint8_t * get_rotated_row(const struct imgconv *state, size_t col,
void *restrict tgt) {
	/* Image has a quarter rotation and may be mirrored. It's not planar,
	 * and any required unpacking was done during initialization. */

	const struct wuimg *src = state->tmp ? state->tmp : state->src;
	size_t pix_size = state->unpack_depth/8 * src->channels;
	ptrdiff_t stride = (ptrdiff_t)wuimg_stride(src);
	uint8_t *data = src->data;
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
	return interleaved_convert(tgt, data, stride, src, state);
}

uint8_t * imgconv_get_row(const struct imgconv *state, size_t y,
void *restrict tgt) {
	if (state->src->mode == image_mode_planar) {
		return planar_convert(state, y, tgt);
	} else if (state->src->rotate & 1) {
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
		data_unpack(tmp->data, src->data,
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
			get_tmp_img(tmp, src, state);
		}
		return st;
	}
	return wu_alloc_error;
}

static bool needs_transfer(const struct wuimg *src) {
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
	color_space_to_linear_sRGB(&src->cs, &state->color,
		src->layout == pix_gray, is_planar, range);
	if (src->cs.type == color_profile_icc) {
		cmsHPROFILE prof = cmsCreate_sRGBProfile();
		if (!prof) {
			return wu_alloc_error;
		}
		const cmsUInt32Number in_fmt = icc_fmt(dst->channels,
			sizeof(float), src->alpha);
		const cmsUInt32Number out_fmt = icc_fmt_colorspace(
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
			fputs("Failed to setup ICC transform, will go on...",
				stderr);
		}
	}
	return wu_ok;
}

const char * imgconv_init(struct imgconv *state, const struct wuimg *dst,
const struct wuimg *src) {
	if (src->attr == pix_float && src->bitdepth < 32) {
		return "Conversion from half-precision floats not yet supported";
	} else if (src->channels > 4) {
		return "Conversion from channels > 4 not supported";
	}

	const bool rm_alpha = src->alpha != alpha_ignore
		&& dst->alpha == alpha_ignore;
	*state = (struct imgconv) {
		.src = src,
		.dst = dst,
		.op = op_noop,
		.unpack_depth = dst->bitdepth,
		.unpack_ch = (uint8_t)(dst->channels + rm_alpha),
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
		state->op = src->bitdepth < dst->bitdepth ? op_unpack : op_noop;
		break;
	case image_mode_bitfield:
		state->op = op_bitfield;
		inrange = outrange;
		break;
	case image_mode_planar:
	case image_mode_raw:
		if (src->attr == pix_float) {
			state->unpack_depth = 32;
			state->op = src->bitdepth > 32 ? op_pack : op_noop;
			inrange = 1;
		} else if (src->bitdepth > dst->bitdepth) {
			state->op = op_pack;
			inrange = fmin(inrange, 0xffff);
		} else if (src->bitdepth < dst->bitdepth || src->attr != pix_normal) {
			state->op = op_unpack;
		}
	}

	enum wu_error st = init_color(state, dst, src, 1.0/inrange);
	if (st != wu_ok) {
		return wu_error_str(st);
	}

	state->transfer = !state->color.eotf.srgb_input || needs_transfer(src);
	state->color.steps &= ~(!state->xfr ? color_step_icc : 0u);
	const enum color_steps omit =
		(!state->transfer ? color_step_eotf : 0)
		| (inrange == outrange ? color_step_map : 0);
	const enum color_steps steps = (state->color.steps & ~omit);
	state->color_passthrough = steps == 0 && src->bitrange == dst->bitrange
		&& src->alpha == alpha_unassociated;

	if (getenv("WU_DEBUG")) {
		fprintf(stderr, "Color steps:"
			" map:%d nonlinear:%d eotf:%d linear:%d icc:%d\n"
			"in-range: %f, out-range: %f\n",
			!!(steps & color_step_map),
			!!(steps & color_step_nonlinear),
			!!(steps & color_step_eotf),
			!!(steps & color_step_linear),
			!!(steps & color_step_icc),
			inrange, outrange);
	}

	/* If we can't read pixels directly, and we can't access rows
	 * independently either, then we must unpack the whole image and
	 * refer to the copy instead of the original. */
	if (state->op != op_noop) {
		if (src->rotate & 1 || src->mode == image_mode_planar) {
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

	/* Allocate row buffer. Buffer is used for
	 * - row unpacking (u8, u16, or float), placed at the end
	 * - colorspace conversion (float), placed at the beginning
	 * - packed output (u8 or u16), placed at the beginning
	 * In case that unpacked data takes as much memory as color corrected
	 * data, it must be offset by a pixel for swizzling to work correctly.
	 * Other than that, stages may overlap with no issues.
	 * FIXME: Don't allocate when none of these steps are neccesary. */
	state->row_len = (row_elems + dst->channels) * sizeof(float),
	state->row = malloc(state->row_len);
	if (!state->row) {
		return "Failed to allocate row memory";
	}

	if (src->mode == image_mode_palette) {
		if (state->color_passthrough && src->layout == dst->layout) {
			state->pal = palette_ref(src->u.palette);
		} else {
			// Create a color-corrected palette
			state->pal = palette_new();
			if (!state->pal) {
				return "Couldn't allocate temporary palette";
			}
			expand_row(state->pal->color, src->u.palette->color,
				4, 1 << src->bitdepth, 4, 8, src, state);
		}
	}
	return NULL;
}
