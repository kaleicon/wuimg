// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "raster/fmt.h"
#include "lib/ea.h"
#include "wudefs.h"

static struct wu_st event_eafnt(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		;struct eafnt_desc *desc = infile->dec_state;
		tree_bud_leaf_u(&infile->metadata, "Characters", desc->chars);
		tree_bud_leaf_u(&infile->metadata, "Image code",
			desc->image_code);
		return WU_OK;
	case ev_subcycle:
		return fmt_load_raster_st(infile->sub_img, infile->ifp);
	default: break;
	}
	return WU_NO_CHANGE;
}
static struct wu_st init_eafnt(struct image_file *infile) {
	return eafnt_init(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn eafnt_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct eafnt_desc),
	.init = init_eafnt,
	.event = event_eafnt,
};
