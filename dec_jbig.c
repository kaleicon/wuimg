#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <jbig.h>

#include "wudefs.h"
#include "common.h"

struct out_info {
	unsigned char *output;
	size_t pos;
	int_fast32_t scale;
};

static void scale_write(unsigned char *restrict data, size_t len,
void *restrict ptr) {
	struct out_info *restrict desc = ptr;
	unsigned char *restrict output = desc->output + desc->pos;
	for (size_t i = 0; i < len; ++i) {
		output[i] = (unsigned char)~((data[i] * desc->scale) >> 16);
	}
	desc->pos += len;
}

enum wu_error jbig_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	unsigned char *data = malloc(BUFSIZ);
	if (!data) {
		return wu_alloc_error;
	}

	struct jbg_dec_state state;
	jbg_dec_init(&state);
	/* Despite the name, this function will not limit the output size if
	 * there isn't a smaller resolution layer. */
	jbg_dec_maxsize(&state, wuconf->max_img_size, wuconf->max_img_size);

	int status = JBG_EAGAIN;
	do {
		const size_t read = fread(data, 1, BUFSIZ, infile->ifp);
		if (!read) {
			break;
		}
		size_t left = read;
		do {
			size_t cnt;
			status = jbg_dec_in(&state, data + read - left,
				left, &cnt);
			left -= cnt;
		} while (left && (status == JBG_EAGAIN));// || status == JBG_EOK));
	} while (status == JBG_EAGAIN);// || status == JBG_EOK);
	free(data);

	switch (status) {
	case JBG_EOK:
	case JBG_EOK_INTR:
		break;
	case JBG_EAGAIN:
		puts("Warning: Expected more data, will continue anyway.");
		break;
	default:
		infile->err_msg = strdup(jbg_strerror(status));
		jbg_dec_free(&state);
		return wu_decoding_error;
	}

	const size_t width = jbg_dec_getwidth(&state);
	const size_t height = jbg_dec_getheight(&state);
	if (zumax(width, height) > wuconf->max_img_size) {
		jbg_dec_free(&state);
		return wu_exceeded_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		jbg_dec_free(&state);
		return wu_alloc_error;
	}

	img->w = width;
	img->h = height;
	img->channels = 1;
	img->bitdepth = (state.planes > 8) ? 16 : 8;
	img->data = malloc(width * height * (img->bitdepth / 8));
	if (!img->data) {
		jbg_dec_free(&state);
		return wu_alloc_error;
	}

	struct out_info info = {
		.output = img->data,
		.pos = 0,
		.scale = fixed_point_scale((1 << img->bitdepth) - 1,
			(1 << state.planes) - 1, 16),
	};
	jbg_dec_merge_planes(&state, false, scale_write, &info);
	jbg_dec_free(&state);
	return wu_ok;
}
