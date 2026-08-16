// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include "lib/sixel.h"
#include "wudefs.h"

static struct wu_st event_sixel(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct sixel_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_u(&infile->metadata, "Horizontal grid size",
			desc->horizontal_grid_size);
		return WU_OK;
	case ev_subcycle:
		return sixel_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_sixel(struct image_file *infile) {
	return sixel_try_parse(infile->dec_state, infile->sub_img, infile->map,
		4096);
}

const struct image_fn sixel_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct sixel_desc),
	.init = init_sixel,
	.event = event_sixel,
};
