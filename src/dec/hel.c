// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/hel.h"

static struct wu_st event_hel(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	if (ev == ev_frame) {
		struct wuimg *img = infile->sub_img;
		int i = img->anim->cur;
		i = i <= state->frame ? i : -1;
		while (i < state->frame) {
			++i;
			struct wu_st st = hel_render_frame(infile->map, img,
				(uint32_t)i);
			if (!wu_isok(st)) {
				return st;
			}
		}
		img->anim->cur = i;
		return WU_OK;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_hel(struct image_file *infile) {
	struct wuimg *img = infile->sub_img;
	struct wu_st st = hel_identify(infile->map, img, 0);
	if (wu_isok(st)) {
		st = WUERR_CHECK(wuimg_alloc_limit(img, infile->conf));
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
