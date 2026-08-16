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
	unsigned char **out = ptr;
	memcpy(*out, data, len);
	*out += len;
}

static struct wu_st parse_jbig(struct image_file *infile,
struct jbg_dec_state *js) {
	if (js->planes > 16) {
		return WUERR_HERE(wu_unsupported_feature);
	}

	struct wuimg *img = infile->sub_img;
	img->w = jbg_dec_getwidth(js);
	img->h = jbg_dec_getheight(js);
	img->channels = 1;
	img->bitdepth = (js->planes > 8) ? 16 : 8;
	img->bitrange = (uint8_t)js->planes;
	img->cs.invert = true;
	return WU_OK;
}

static void end_jbig(struct image_file *infile) {
	jbg_dec_free(infile->dec_state);
}

static struct wu_st event_jbig(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		return parse_jbig(infile, infile->dec_state);
	case ev_subcycle:
		;unsigned char *out = infile->sub_img->data;
		jbg_dec_merge_planes(infile->dec_state, false,
			scale_jbig_write, &out);
		return WU_OK;
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_jbig(struct image_file *infile) {
	jbg_dec_init(infile->dec_state);
	unsigned char *why_isnt_it_const = (unsigned char *)infile->map.ptr;
	const int status = jbg_dec_in(infile->dec_state, why_isnt_it_const,
		infile->map.len, NULL);
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
	return WU_OK;
}

const struct image_fn jbig_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct jbg_dec_state),
	.init = init_jbig,
	.event = event_jbig,
	.end = end_jbig,
};
