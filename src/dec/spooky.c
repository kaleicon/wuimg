// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/spooky.h"

static size_t dec(const void *ptr, struct wuimg *img) {
	return tre_decode(ptr, img);
}
static enum wu_error parse(void *ptr, struct wuimg *img) {
	return tre_parse(ptr, img);
}
static enum wu_error init(void *ptr, struct image_file *infile) {
	return tre_init(ptr, infile->map);
}

static enum wu_error tre_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct tre_desc desc;
	return rast_trivial_dec(infile, conf, &desc, init, parse, NULL, dec,
		NULL);
}

static enum wu_error trs_callback(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event _ev) {
	(void)_ev;
	const uint16_t i = (uint16_t)state->idx;
	struct wuimg *img = infile->sub_img + i;
	struct trs_desc *desc = infile->dec_state;

	const enum wu_error st = trs_set_image(desc, img, i);
	if (st == wu_ok) {
		if (!wuimg_exceeds_limit(img, conf)) {
			return trs_get_image(desc, img, i)
				? wu_ok : wu_decoding_error;
		}
		return wu_exceeds_size_limit;
	}
	return st;
}

static enum wu_error trs_dec(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	struct trs_desc *desc = calloc(1, sizeof(*desc));
	if (desc) {
		infile->dec_state = desc;
		const enum wu_error st = trs_init(desc, infile->map);
		if (st == wu_ok) {
			return alloc_sub_images(infile, desc->nr)
				? wu_ok : wu_alloc_error;
		}
		return st;
	}
	return wu_alloc_error;
}

const struct image_fn tre_fn = {
	.mmap = true,
	.dec = tre_dec,
};
const struct image_fn trs_fn = {
	.mmap = true,
	.dec = trs_dec,
	.callback = trs_callback,
	.end = image_file_end,
};
