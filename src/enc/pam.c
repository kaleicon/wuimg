// SPDX-License-Identifier: 0BSD
#include <inttypes.h>

#include "misc/bit.h"
#include "misc/endian.h"

#include "pam.h"

size_t pam_write_row(uint8_t *restrict row, const struct wuimg *dst, FILE *ofp) {
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

void pam_write_header(const struct wuimg *dst, FILE *ofp) {
	const uint32_t maxval = bit_set32(dst->bitrange);
	fprintf(ofp,
		"P7\n"
		"WIDTH %zu\n"
		"HEIGHT %zu\n"
		"DEPTH %hhu\n"
		"MAXVAL %" PRIu16 "\n",
		dst->w, dst->h, dst->channels, (uint16_t)maxval);
	write_tuple(dst->channels, ofp);
	fputs("ENDHDR\n", ofp);
}

void pam_best_fit(struct wuimg *dst, const struct wuimg *src) {
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
