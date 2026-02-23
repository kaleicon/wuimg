// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "misc/math.h"
#include "lib/croteam.h"

static struct wu_st event_tbn(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	if (ev == ev_subcycle || ev == ev_frame) {
		return tbn_frame(infile->dec_state, infile->sub_img,
			(uint32_t)state->frame);
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_tbn(struct image_file *infile) {
	struct tbn_desc *desc = infile->dec_state;
	struct wu_st st = tbn_init(desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		struct wutree *res = tree_add_branch(&infile->metadata,
			"Resolution");
		if (res) {
			tree_bud_leaf_u(res, "X", desc->xres);
			tree_bud_leaf_u(res, "Y", desc->yres);
		}
		tree_bud_leaf_u(&infile->metadata, "Shift right", desc->shr);
		tree_bud_leaf_u(&infile->metadata, "Mystery val", desc->unknown);
		tree_bud_leaf_u(&infile->metadata, "Mystery flag", desc->flags);

		char anima[TBN_ANIMADAT_LEN];
		size_t len = tbn_read_animadat(desc, anima);
		tree_add_leaf_len(&infile->metadata, "ANIMADAT",
			wuptr_trim_end(wuptr_mem(anima, len), 0), NULL);
	}
	return st;
}

const struct image_fn tbn_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct tbn_desc),
	.init = init_tbn,
	.event = event_tbn,
};
