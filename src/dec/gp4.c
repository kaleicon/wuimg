// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/gp4.h"

static struct wu_st event_gp4(struct image_file *infile, struct wu_state *state,
const enum image_event ev) {
	(void)state;
	struct gp4_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_u(&infile->metadata, "X", desc->x);
		tree_bud_leaf_u(&infile->metadata, "Y", desc->y);
		return WU_OK;
	case ev_subcycle:
		return gp4_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_gp4(struct image_file *infile) {
	return gp4_parse(infile->dec_state, infile->map, infile->sub_img);
}

const struct image_fn gp4_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct gp4_desc),
	.init = init_gp4,
	.event = event_gp4,
};
