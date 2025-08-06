// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/hel.h"

static struct wu_st event_hel(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c;
	if (ev == ev_frame) {
		struct wuimg *img = infile->sub_img;
		int i = img->frames->current;
		i = state->frame < i ? 0 : i + 1;
		while (i <= state->frame) {
			struct wu_st st = hel_render_frame(infile->map, img,
				(uint32_t)i);
			++i;
			if (!wu_isok(st)) {
				return st;
			}
		}
		img->frames->current = state->frame;
		return WU_OK;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_hel(struct image_file *infile,
const struct wu_conf *conf) {
	struct wuimg *img = infile->sub_img;
	struct wu_st st = hel_identify(infile->map, img, 0);
	if (wu_isok(st)) {
		st = WUERR_CHECK(wuimg_alloc_limit(img, conf));
		if (wu_isok(st)) {
			st = hel_render_frame(infile->map, img, 0);
		}
	}
	return st;
}

const struct image_fn hel_fn = {
	.alloc_single = true,
	.mmap = true,
	.init = init_hel,
	.event = event_hel,
};
