// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include "lib/eri.h"
#include "wudefs.h"

static struct wu_st event_eri(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct eri_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_u(&infile->metadata, "Block size",
			1u << desc->blocking_degree);
		return WU_OK;
	case ev_subcycle:
		return eri_decode(desc);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_eri(struct image_file *infile) {
	return eri_init(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn eri_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct eri_desc),
	.init = init_eri,
	.event = event_eri,
};
