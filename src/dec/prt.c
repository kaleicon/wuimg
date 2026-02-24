// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/prt.h"

static void end_prt(struct image_file *infile) {
	prt_cleanup(infile->dec_state);
}

static struct wu_st event_prt(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct prt_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		;struct wutree *tree = &infile->metadata;
		tree_bud_leaf_u(tree, "Version", desc->version);
		tree_bud_leaf_u(tree, "Depth", desc->depth);
		tree_bud_leaf_bool(tree, "Mask", desc->mask);
		if (desc->version == prt_v102) {
			tree_bud_leaf_u(tree, "X", desc->x);
			tree_bud_leaf_u(tree, "Y", desc->y);
		}
		return WU_OK;
	case ev_subcycle:
		return prt_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_prt(struct image_file *infile) {
	return prt_parse(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn prt_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct prt_desc),
	.init = init_prt,
	.event = event_prt,
	.end = end_prt,
};
