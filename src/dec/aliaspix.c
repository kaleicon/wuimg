// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/aliaspix.h"
#include "wudefs.h"

static struct wu_st event_aliaspix(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct aliaspix_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_u(&infile->metadata, "X", desc->x);
		tree_bud_leaf_u(&infile->metadata, "Y", desc->y);
		return WU_OK;
	case ev_subcycle:
		return aliaspix_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_aliaspix(struct image_file *infile) {
	return aliaspix_init(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn aliaspix_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct aliaspix_desc),
	.init = init_aliaspix,
	.event = event_aliaspix,
};
