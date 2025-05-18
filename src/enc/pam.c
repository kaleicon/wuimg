// SPDX-License-Identifier: 0BSD
#include <inttypes.h>

#include "misc/bit.h"
#include "misc/endian.h"

#include "enc.h"

static size_t write_row(void *state, const struct wuimg *dst, FILE *ofp,
uint8_t *restrict row) {
	(void)state;
	const size_t len = dst->w * dst->channels;
	if (dst->bitdepth == 16) {
		endian_loop16((uint16_t *)row, big_endian, len);
	}
	return fwrite(row, dst->bitdepth/8, len, ofp);
}

static size_t write_frame(void *state, const struct wuimg *dst, FILE *ofp,
const int frame) {
	(void)state; (void)frame;
	const size_t len = dst->w*dst->channels;
	ptrdiff_t stride = (ptrdiff_t)wuimg_stride(dst);
	const uint8_t *data = dst->data
		+ stride * (dst->mirror ? (ptrdiff_t)(dst->h - 1) : 0);
	stride *= dst->mirror ? -1 : 1;
	size_t w = 0;
	for (size_t y = 0; y < dst->h; ++y) {
		w += fwrite(data, dst->bitdepth/8, len, ofp);
		data += stride;
	}
	return w;
}

static void write_tuple(const uint8_t ch, FILE *ofp) {
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

static const char * init_pam(void *state, const struct wuimg *dst,
const struct wuimg *src, FILE *ofp) {
	(void)state; (void)src;
	const uint32_t maxval = bit_set32(dst->bitrange);
	fprintf(ofp,
		"P7\n"
		"WIDTH %zu\n"
		"HEIGHT %zu\n"
		"DEPTH %hhu\n"
		"MAXVAL %" PRIu16 "\n",
		dst->w, dst->h, dst->channels, (uint16_t)maxval);
	write_tuple(dst->channels, ofp);
	return fputs("ENDHDR\n", ofp) > 0 ? NULL : "output error";
}

static bool passthrough(const struct wuimg *src) {
	switch (src->bitdepth) {
	case 16:
		if (which_end() != big_endian) {
			return false;
		}
		break;
	case 8: break;
	default: return false;
	}

	const enum pix_layout l_expect = src->channels >= 3
		? pix_rgba : pix_gray;
	const enum alpha_interpretation a_expect = (src->channels & 1)
		? alpha_ignore : alpha_unassociated;
	const struct color_space *cs = &src->cs;
	return src->layout == l_expect
		&& src->alpha == a_expect
		&& src->attr == pix_normal
		&& src->rotate == 0
		&& (cs->primaries == 0 || cs->primaries == cicp_primaries_bt709_6)
		&& (cs->transfer == 0 || cs->transfer == cicp_transfer_iec_61966_2_1)
		&& cs->matrix == cicp_matrix_rgb
		&& !cs->limited
		&& cs->type == color_profile_enum;
}

static bool best_fit(struct wuimg *dst, const struct wuimg *src) {
	dst->w = (src->rotate & 1) ? src->h : src->w;
	dst->h = (src->rotate & 1) ? src->w : src->h;
	dst->alpha = alpha_unassociated;
	switch (src->mode) {
	case image_mode_raw:
	case image_mode_planar:
		dst->channels = src->channels;
		dst->bitdepth = src->bitdepth > 8 ? 16 : 8;
		if (src->mode == image_mode_raw) {
			return passthrough(src);
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
	.best_fit = best_fit,
	.init = init_pam,
	.write_row = write_row,
	.write_frame = write_frame,
};
