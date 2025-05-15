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

static void best_fit(struct wuimg *dst, const struct wuimg *src) {
	dst->w = (src->rotate & 1) ? src->h : src->w;
	dst->h = (src->rotate & 1) ? src->w : src->h;
	switch (src->mode) {
	case image_mode_raw:
	case image_mode_planar:
		dst->channels = src->channels;
		dst->bitdepth = src->bitdepth > 8 ? 16 : 8;
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
	dst->alpha = alpha_unassociated;
}

const struct enc_fn pam_enc = {
	.best_fit = best_fit,
	.init = init_pam,
	.write_row = write_row,
};
