// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/cbg.h"

static struct wu_st event_cbg(struct image_file *infile, struct wu_state *state,
const enum image_event ev) {
	(void)state;
	struct cbg_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_u(&infile->metadata, "Version", desc->version);
		return WU_OK;
	case ev_subcycle:
		return cbg_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_cbg(struct image_file *infile) {
	return cbg_parse(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn cbg_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct cbg_desc),
	.init = init_cbg,
	.event = event_cbg,
};
