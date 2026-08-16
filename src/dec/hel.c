// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/hel.h"

static struct wu_st event_hel(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct wuimg *img = infile->sub_img;
	switch (ev) {
	case ev_metadata:
		return hel_identify(infile->map, infile->sub_img, 0);
	case ev_subcycle:
		return hel_render_frame(infile->map, img, 0);
	case ev_frame:
		;int i = img->anim->cur;
		i = i <= state->frame ? i : -1;
		struct wu_st st = WU_OK;
		while (i < state->frame) {
			++i;
			st = hel_render_frame(infile->map, img, (uint32_t)i);
			if (!wu_isok(st)) {
				break;
			}
		}
		img->anim->cur = i;
		return st;
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn hel_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.event = event_hel,
};
