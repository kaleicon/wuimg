// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/kyg.h"

static void get_kyg_meta(const struct kyg_desc *desc, struct wutree *metadata) {
	tree_add_leaf_len(metadata, "Comment", desc->comment, "SHIFT-JIS");
	tree_bud_leaf_u(metadata, "X", desc->x);
	tree_bud_leaf_u(metadata, "Y", desc->y);
}

static struct wu_st event_kyg(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		get_kyg_meta(infile->dec_state, &infile->metadata);
		return WU_OK;
	case ev_subcycle:
		return kyg_decode(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_kyg(struct image_file *infile) {
	return kyg_parse(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn kyg_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct kyg_desc),
	.init = init_kyg,
	.event = event_kyg,
};
