#include <jbig.h>

#include "common/file.h"
#include "raster/strip.h"
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
		image_file_status_append(infile, wu_unexpected_eof);
		break;
	default:
		image_file_error_append(infile, jbg_strerror(status));
		return wu_decoding_error;
	}

	if (state->planes > 16) {
		return wu_unsupported_feature;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	img->w = jbg_dec_getwidth(state);
	img->h = jbg_dec_getheight(state);
	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	img->channels = 1;
	img->bitdepth = (state->planes > 8) ? 16 : 8;
	img->used_bits = (uint8_t)state->planes;
	img->attr = pix_inverted;
	const enum wu_error st = raw_img_alloc(img);
	if (st == wu_ok) {
		struct out_info out = {.output = img->data};
		jbg_dec_merge_planes(state, false, scale_write, &out);
	}
	return st;
}

enum wu_error jbig_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct map_info mm;
	if (!file_map(&mm, infile->ifp)) {
		return wu_alloc_error;
	}

	struct jbg_dec_state state;
	jbg_dec_init(&state);
	unsigned char *why_isnt_it_const = (unsigned char *)mm.data;
	const int status = jbg_dec_in(&state, why_isnt_it_const, mm.len, NULL);
	file_unmap(&mm);
	const enum wu_error st = dec_wrap(infile, wuconf, &state, status);
	jbg_dec_free(&state);
	return st;
}
