// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/eclipse.h"
#include "wudefs.h"

static struct wu_st event_eclipse(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct eclipse_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_u(&infile->metadata, "Version", desc->version);
		tree_add_leaf_utf8_limit(&infile->metadata, "Software",
			wuptr_mem(desc->software, sizeof(desc->software)));
		tree_add_leaf_utf8_limit(&infile->metadata, "Revision",
			wuptr_mem(desc->revision, sizeof(desc->revision)));
		return WU_OK;
	case ev_subcycle:
		return eclipse_load(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}
static struct wu_st init_eclipse(struct image_file *infile) {
	return eclipse_init(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn eclipse_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct eclipse_desc),
	.init = init_eclipse,
	.event = event_eclipse,
};
