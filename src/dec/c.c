// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/c.h"

static void get_c_metadata(struct wutree *tree, const struct c_desc *desc) {
	tree_add_leaf_utf8(tree, "Type", c_type_str(desc));
	if (desc->fmt == c_xbm && desc->xbm.has_hotspot) {
		struct wutree *hot = tree_add_branch(tree, "Hot spot");
		if (hot) {
			tree_bud_leaf_d(hot, "X", desc->xbm.x_hot);
			tree_bud_leaf_d(hot, "Y", desc->xbm.y_hot);
		}
	}
}

static struct wu_st event_c(struct image_file *infile, struct wu_state *state,
const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		get_c_metadata(&infile->metadata, infile->dec_state);
		return WU_OK;
	case ev_subcycle:
		return c_decode(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_c(struct image_file *infile) {
	return c_parse(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn c_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct c_desc),
	.init = init_c,
	.event = event_c,
};
