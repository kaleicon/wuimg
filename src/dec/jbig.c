// SPDX-License-Identifier: 0BSD
#include <jbig.h>

#include "wudefs.h"

struct out_info {
	unsigned char *output;
};

static void scale_write(unsigned char *restrict data, size_t len,
void *restrict ptr) {
	struct out_info *restrict p = ptr;
	memcpy(p->output, data, len);
	p->output += len;
}

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, struct jbg_dec_state *state, const int status) {
	switch (status) {
	case JBG_EOK:
	case JBG_EOK_INTR:
		break;
	case JBG_EAGAIN:
		image_file_error_append(infile, wu_unexpected_eof);
		break;
	default:
		image_file_strerror_append(infile, jbg_strerror(status));
		return wu_decoding_error;
	}

	if (state->planes > 16) {
		return wu_unsupported_feature;
	}

	struct wuimg *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	img->w = jbg_dec_getwidth(state);
	img->h = jbg_dec_getheight(state);
	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	img->channels = 1;
	img->bitdepth = (state->planes > 8) ? 16 : 8;
	img->used_bits = (uint8_t)state->planes;
	img->attr = pix_inverted;
	const enum wu_error st = wuimg_alloc(img);
	if (st == wu_ok) {
		struct out_info out = {.output = img->data};
		jbg_dec_merge_planes(state, false, scale_write, &out);
	}
	return st;
}

static enum wu_error jbig_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct jbg_dec_state state;
	jbg_dec_init(&state);
	unsigned char *why_isnt_it_const = (unsigned char *)infile->map.data;
	const int status = jbg_dec_in(&state, why_isnt_it_const,
		infile->map.len, NULL);
	const enum wu_error st = dec_wrap(infile, wuconf, &state, status);
	jbg_dec_free(&state);
	return st;
}

const struct image_fn jbig_fn = {.mmap = true, .dec = jbig_dec};
