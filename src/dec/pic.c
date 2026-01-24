// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/pic.h"

static struct wu_st event_pic(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf; (void)state;
	struct pic_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		;struct wutree *tree = &infile->metadata;
		tree_add_leaf_len(tree, "Comment", desc->comm, "SHIFT-JIS");
		tree_add_leaf_len(tree, "Dummy", desc->dummy, NULL);
		tree_add_leaf_utf8(tree, "Model", pic_model_str(desc->type));
		tree_bud_leaf_u(tree, "Mode", desc->mode);
		tree_bud_leaf_u(tree, "Depth", desc->depth);
		tree_bud_leaf_d(tree, "X", desc->x);
		tree_bud_leaf_d(tree, "Y", desc->y);
		if (desc->tiled) {
			tree_bud_leaf_bool(tree, "Tiled", true);
		}
		return WU_OK;
	case ev_subcycle:
		return pic_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_pic(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	return pic_parse(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn pic_fn = {
	.mmap = true,
	.state_size = sizeof(struct pic_desc),
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.init = init_pic,
	.event = event_pic,
};
