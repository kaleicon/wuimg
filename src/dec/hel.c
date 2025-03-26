// SPDX-License-Identifier: 0BSD
#include "wudefs.h"
#include "lib/hel.h"

static enum wu_error hel_callback(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf;
	enum wu_error st = wu_no_change;
	if (ev == ev_frame) {
		struct wuimg *img = infile->sub_img;
		const int current = img->frames->current;
		int i = state->frame < current ? 0 : current + 1;
		while (i <= state->frame) {
			const size_t w = hel_render_frame(infile->map, img,
				(uint32_t)i);
			++i;
			if (!w) {
				return wu_decoding_error;
			}
		}
		img->frames->current = i;
		return wu_ok;
	}
	return st;
}

static enum wu_error hel_init(struct image_file *infile,
const struct wu_conf *conf) {
	struct wuimg *img = infile->sub_img;
	const enum wu_error st = hel_identify(infile->map, img, 0);
	if (st == wu_ok) {
		if (!wuimg_exceeds_limit(img, conf)) {
			if (wuimg_alloc_noverify(img)) {
				return hel_render_frame(infile->map, img, 0)
					? wu_ok : wu_decoding_error;
			}
			return wu_alloc_error;
		}
		return wu_exceeds_size_limit;
	}
	return st;
}

const struct image_fn hel_fn = {
	.alloc_single = true,
	.mmap = true,
	.dec = hel_init,
	.callback = hel_callback,
};
