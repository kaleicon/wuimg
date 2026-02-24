// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/txf.h"

static struct wu_st event_txf(struct image_file *infile,
struct wu_state *state, enum image_event ev) {
	(void)state;
	struct txf_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_u(&infile->metadata, "Max ascent",
			desc->max_ascent);
		tree_bud_leaf_u(&infile->metadata, "Max descent",
			desc->max_descent);
		return WU_OK;
	case ev_subcycle:
		return txf_load(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_txf(struct image_file *infile) {
	return txf_parse(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn txf_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct txf_desc),
	.init = init_txf,
	.event = event_txf,
};
