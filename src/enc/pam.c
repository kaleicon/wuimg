// SPDX-License-Identifier: 0BSD
#include <inttypes.h>

#include "misc/bit.h"
#include "misc/endian.h"

#include "pam.h"

size_t pam_write_row(const struct wuimg *out, FILE *ofp) {
	const size_t len = out->w * out->channels;
	if (out->bitdepth == 16) {
		endian_loop16((uint16_t *)out->data, big_endian, len);
	}
	return fwrite(out->data, out->bitdepth/8, len, ofp);
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

void pam_write_header(const struct wuimg *out, FILE *ofp) {
	const uint32_t maxval = bit_set32(out->used_bits);
	fprintf(ofp,
		"P7\n"
		"WIDTH %zu\n"
		"HEIGHT %zu\n"
		"DEPTH %hhu\n"
		"MAXVAL %" PRIu16 "\n",
		out->w, out->h, out->channels, (uint16_t)maxval);
	write_tuple(out->channels, ofp);
	fputs("ENDHDR\n", ofp);
}

static bool unassociated_or_no_alpha(const struct wuimg *in) {
	return in->alpha == alpha_unassociated
		|| pix_layout_offset(in->layout, pix_alpha) >= in->channels;
}

bool pam_can_cpy(struct wuimg *out, const struct wuimg *in) {
	if (in->mode == image_mode_raw && in->attr == pix_normal
	&& in->rotate == 0 && !in->mirror && unassociated_or_no_alpha(in)
	&& color_space_is_sRGB(&in->cs)) {
		switch (in->bitdepth) {
		case 8: case 16:
			out->w = in->w;
			out->h = in->h;
			out->channels = in->channels;
			out->bitdepth = in->bitdepth;
			out->used_bits = in->used_bits;
			return true;
		}
	}
	return false;
}
