// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <string.h>

#include <jbig.h>

#include "wudefs.h"

struct out_info {
	unsigned char *output;
};

static void scale_jbig_write(unsigned char *restrict data, size_t len,
void *restrict ptr) {
	struct out_info *restrict p = ptr;
	memcpy(p->output, data, len);
	p->output += len;
}

static struct wu_st wrap_jbig_dec(struct image_file *infile,
struct jbg_dec_state *state, const int status) {
	switch (status) {
	case JBG_EOK:
	case JBG_EOK_INTR:
		break;
	case JBG_EAGAIN:
		image_file_error_append(infile, wu_unexpected_eof);
		break;
	default:
		image_file_strerror_append(infile, jbg_strerror(status));
		return WUERR_HERE(wu_decoding_error);
	}

	if (state->planes > 16) {
		return WUERR_HERE(wu_unsupported_feature);
	}

	struct wuimg *img = infile->sub_img;
	img->w = jbg_dec_getwidth(state);
	img->h = jbg_dec_getheight(state);
	img->channels = 1;
	img->bitdepth = (state->planes > 8) ? 16 : 8;
	img->bitrange = (uint8_t)state->planes;
	img->cs.invert = true;
	const enum wu_error err = wuimg_alloc_limit(img, infile->conf);
	if (err == wu_ok) {
		struct out_info out = {.output = img->data};
		jbg_dec_merge_planes(state, false, scale_jbig_write, &out);
		return WU_OK;
	}
	return WUERR_HERE(err);
}

static struct wu_st init_jbig(struct image_file *infile) {
	struct jbg_dec_state state;
	jbg_dec_init(&state);
	unsigned char *why_isnt_it_const = (unsigned char *)infile->map.ptr;
	const int status = jbg_dec_in(&state, why_isnt_it_const,
		infile->map.len, NULL);
	const struct wu_st st = wrap_jbig_dec(infile, &state, status);
	jbg_dec_free(&state);
	return st;
}

const struct image_fn jbig_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_jbig
};
