// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include "lib/pictor.h"
#include "wudefs.h"

static struct wu_st event_pictor(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct pictor_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		;struct wutree *tree = &infile->metadata;
		tree_bud_leaf_u(tree, "X", desc->x);
		tree_bud_leaf_u(tree, "Y", desc->y);
		tree_bud_leaf_u(tree, "Blocks", desc->blocks);
		tree_bud_leaf_u(tree, "Planes", desc->planes);
		tree_bud_leaf_u(tree, "Depth", desc->depth);

		tree_add_leaf_utf8(tree, "Video mode",
			pictor_video_mode(desc));
		tree_add_leaf_utf8(tree, "Palette type",
			pictor_palette_str(desc->pal_type));
		return WU_OK;
	case ev_subcycle:
		return pictor_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_pictor(struct image_file *infile) {
	return pictor_parse(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn pictor_fn = {
	.state_size = sizeof(struct pictor_desc),
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.init = init_pictor,
	.event = event_pictor,
};
