// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include "wudefs.h"
#include "lib/pi.h"

static void get_pi_metadata(const struct pi_desc *desc, struct wutree *tree) {
	if (!desc->lsp) {
		tree_add_leaf_len(tree, "Comment", desc->comm, "SHIFT-JIS");
		tree_add_leaf_len(tree, "Dummy", desc->dummy, NULL);
		tree_add_leaf_len(tree, "Saver model",
			WUPTR_ARRAY(desc->saver.model), "SHIFT-JIS");
		tree_add_leaf_len(tree, "Saver data",
			desc->saver.data, "SHIFT-JIS");
	}
	tree_bud_leaf_u(tree, "Depth", desc->depth);
}

static struct wu_st event_pi(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		get_pi_metadata(infile->dec_state, &infile->metadata);
		return WU_OK;
	case ev_subcycle:
		return pi_decode(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_pi(struct image_file *infile) {
	return pi_read_header(infile->dec_state, infile->sub_img, infile->map,
		infile->ext);
}


static struct wu_st event_dpc(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct dpc_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		if (desc->data.len) {
			tree_bud_leaf_u(&infile->metadata, "X", desc->x);
			tree_bud_leaf_u(&infile->metadata, "Y", desc->y);
		}
		return WU_OK;
	case ev_subcycle:
		return dpc_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_dpc(struct image_file *infile) {
	return dpc_read_header(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn pi_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct pi_desc),
	.init = init_pi,
	.event = event_pi,
};
const struct image_fn dpc_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct dpc_desc),
	.init = init_dpc,
	.event = event_dpc,
};
