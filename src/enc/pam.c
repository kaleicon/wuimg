// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <inttypes.h>
#include <stdlib.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"

#include "enc.h"

struct pam_state {
	uint16_t *buf;
};

static void end_pam(void *ptr) {
	struct pam_state *state = ptr;
	free(state->buf);
}

static size_t write_pam_row(void *ptr, const struct wuimg *dst, FILE *ofp,
uint8_t *restrict row) {
	(void)ptr;
	const size_t len = dst->w * dst->channels;
	if (dst->bitdepth == 16) {
		endian_loop16((uint16_t *)row, big_endian, len);
	}
	return fwrite(row, dst->bitdepth/8, len, ofp);
}

static size_t write_pam_frame(void *ptr, const struct wuimg *dst, FILE *ofp,
const int frame) {
	(void)frame;
	struct pam_state *state = ptr;
	const size_t len = dst->w*dst->channels;
	ptrdiff_t stride = (ptrdiff_t)wuimg_stride(dst);
	const uint8_t *data = dst->data
		+ stride * (dst->mirror ? (ptrdiff_t)(dst->h - 1) : 0);
	stride *= dst->mirror ? -1 : 1;
	size_t w = 0;
	for (size_t y = 0; y < dst->h; ++y) {
		const void *pix = data;
		if (state->buf) {
			const uint16_t *row16 = pix;
			for (size_t i = 0; i < len; ++i) {
				state->buf[i] = endian16(row16[i], big_endian);
			}
			pix = state->buf;
		}
		w += fwrite(pix, dst->bitdepth/8, len, ofp);
		data += stride;
	}
	return w;
}

static void write_pam_tuple(const uint8_t ch, FILE *ofp) {
	const char *tupl;
	switch (ch) {
	case 1: tupl = "GRAYSCALE"; break;
	case 2: tupl = "GRAYSCALE_ALPHA"; break;
	case 3: tupl = "RGB"; break;
	case 4: tupl = "RGB_ALPHA"; break;
	default: return;
	}
	fprintf(ofp, "TUPLTYPE %s\n", tupl);
}

static const char * init_pam(void *ptr, const struct wuimg *dst,
const struct wuimg *src, FILE *ofp) {
	struct pam_state *state = ptr;
	if (dst == src && dst->bitdepth == 16 && which_end() != big_endian) {
		state->buf = malloc(wuimg_stride(dst));
		if (!state->buf) {
			return "failed to allocate PAM buffer";
		}
	} else {
		state->buf = NULL;
	}
	const uint32_t maxval = bit_set32(dst->bitrange);
	fprintf(ofp,
		"P7\n"
		"WIDTH %zu\n"
		"HEIGHT %zu\n"
		"DEPTH %hhu\n"
		"MAXVAL %" PRIu16 "\n",
		dst->w, dst->h, dst->channels, (uint16_t)maxval);
	write_pam_tuple(dst->channels, ofp);
	return fputs("ENDHDR\n", ofp) > 0 ? NULL : "output error";
}

static bool passthrough_pam(const struct wuimg *src) {
	switch (src->bitdepth) {
	case 8: case 16: break;
	default: return false;
	}

	const enum pix_layout l_expect = src->channels >= 3
		? pix_rgba : pix_gray;
	const enum alpha_interpretation a_expect = (src->channels & 1)
		? alpha_ignore : alpha_unassociated;
	return src->align_sh != align_bitpack
		&& src->layout == l_expect
		&& src->alpha == a_expect
		&& src->attr == pix_normal
		&& src->rotate == 0
		&& color_space_is_sRGB(&src->cs);
}

static bool best_pam_fit(struct wuimg *dst, const struct wuimg *src) {
	dst->w = (src->rotate & 1) ? src->h : src->w;
	dst->h = (src->rotate & 1) ? src->w : src->h;
	dst->alpha = alpha_unassociated;
	switch (src->mode) {
	case image_mode_raw:
	case image_mode_planar:
		dst->channels = src->channels;
		dst->bitdepth = src->bitdepth > 8 ? 16 : 8;
		if (src->mode == image_mode_raw) {
			return passthrough_pam(src);
		}
		break;
	case image_mode_palette:
		dst->channels = 4;
		dst->bitdepth = 8;
		break;
	case image_mode_bitfield:
		dst->channels = src->u.bitfield->ch;
		dst->bitdepth = src->u.bitfield->outdepth;
		break;
	}
	return false;
}

const struct enc_fn pam_enc = {
	.state_size = sizeof(struct pam_state),
	.best_fit = best_pam_fit,
	.init = init_pam,
	.write_row = write_pam_row,
	.write_frame = write_pam_frame,
	.end = end_pam,
};


static size_t dump_raw_row(void *state, const struct wuimg *dst, FILE *ofp,
uint8_t *restrict row) {
	(void)state;
	return fwrite(row, 1, wuimg_stride(dst), ofp);
}

static size_t dump_raw_frame(void *state, const struct wuimg *dst, FILE *ofp,
const int frame) {
	(void)state; (void)frame;
	size_t w = 0;
	switch (dst->mode) {
	case image_mode_palette:
		// Always hash the palette in case it's modified between frames
		;const struct palette *pal = dst->u.palette;
		w += fwrite(pal->color, 1, sizeof(pal->color), ofp);
		// fallthrough
	case image_mode_raw:
	case image_mode_bitfield:
		w += fwrite(dst->data, 1, wuimg_size(dst), ofp);
		break;
	case image_mode_planar:
		;const struct plane_info *p = dst->u.planes->p;
		for (size_t z = 0; z < dst->channels; ++z) {
			w += fwrite(p[z].ptr, 1, p[z].size, ofp);
		}
		break;
	}
	return w;
}

static const char * no_need_for_this(void *state, const struct wuimg *dst,
const struct wuimg *src, FILE *ofp) {
	(void)state; (void)dst; (void)src; (void)ofp;
	return NULL;
}

const struct enc_fn raw_enc = {
	.support = enc_subimg,
	.best_fit = best_pam_fit,
	.init = no_need_for_this,
	.write_row = dump_raw_row,
	.write_frame = dump_raw_frame,
	.end = null_function,
};
