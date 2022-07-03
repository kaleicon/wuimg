#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <jbig.h>

#include "../wudefs.h"
#include "../common.h"

struct out_info {
	unsigned char *output;
	size_t pos;
	uint_fast32_t scale;
};

static void scale_write(unsigned char *restrict data, size_t len,
void *restrict ptr) {
	struct out_info *restrict desc = ptr;
	unsigned char *restrict output = desc->output + desc->pos;
	for (size_t i = 0; i < len; ++i) {
		output[i] = (unsigned char)((data[i] * desc->scale) >> 16);
	}
	desc->pos += len;
}

enum wu_error jbig_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct map_info mm;
	if (!map_file(&mm, infile->ifp)) {
		return wu_alloc_error;
	}

	struct jbg_dec_state state;
	jbg_dec_init(&state);
	unsigned char *why_isnt_it_const = (unsigned char *)mm.data;
	const int status = jbg_dec_in(&state, why_isnt_it_const, mm.len, NULL);
	unmap_file(&mm);

	switch (status) {
	case JBG_EOK:
	case JBG_EOK_INTR:
		break;
	case JBG_EAGAIN:
		puts("Warning: Expected more data, will continue anyway.");
		break;
	default:
		image_file_error_append(infile, jbg_strerror(status));
		jbg_dec_free(&state);
		return wu_decoding_error;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		jbg_dec_free(&state);
		return wu_alloc_error;
	}

	img->w = jbg_dec_getwidth(&state);
	img->h = jbg_dec_getheight(&state);
	if (raw_img_exceeds_limit(img, wuconf)) {
		jbg_dec_free(&state);
		return wu_exceeds_size_limit;
	}
	img->channels = 1;
	img->bitdepth = (state.planes > 8) ? 16 : 8;
	img->attr = pix_inverted;
	const enum wu_error st = raw_img_alloc(img);
	if (st == wu_ok) {
		const uint_fast32_t range = (1U << img->bitdepth) - 1;
		const uint_fast32_t maxval = (1U << state.planes) - 1;
		struct out_info info = {
			.output = img->data,
			.pos = 0,
			.scale = (range << 16) / maxval + 1,
		};
		jbg_dec_merge_planes(&state, false, scale_write, &info);
	}
	jbg_dec_free(&state);
	return st;
}
