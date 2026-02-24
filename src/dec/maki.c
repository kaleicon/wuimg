// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/maki.h"

static struct wu_st event_maki(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct maki_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		;struct wutree *tree = &infile->metadata;
		tree_add_leaf_utf8(tree, "Version",
			maki_version_str(desc->version));
		tree_add_leaf_len(tree, "Model",
			WUPTR_ARRAY(desc->model), "SHIFT-JIS");
		tree_add_leaf_len(tree, "Comment",
			WUPTR_ARRAY(desc->comment), "SHIFT-JIS");
		tree_bud_leaf_u(tree, "X", desc->x);
		tree_bud_leaf_u(tree, "Y", desc->y);
		return WU_OK;
	case ev_subcycle:
		return maki_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_maki(struct image_file *infile) {
	return maki_parse(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn maki_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct maki_desc),
	.init = init_maki,
	.event = event_maki,
};
