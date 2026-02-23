// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/dvm.h"

static void end_dvm(struct image_file *infile) {
	dvm_cleanup(infile->dec_state);
}

static struct wu_st event_dvm(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct dvm_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	switch (ev) {
	case ev_metadata:
		tree_add_leaf_len(&infile->metadata, "Comment",
			wuptr_wustr(desc->text), NULL);
		return WU_OK;
	case ev_subcycle:
	case ev_frame:
		return dvm_load_frame(desc, img, (size_t)state->frame);
	default: break;
	}
	return wuerr(wu_no_change, NULL);
}

static struct wu_st init_dvm(struct image_file *infile) {
	return dvm_parse(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn dvm_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct dvm_desc),
	.init = init_dvm,
	.event = event_dvm,
	.end = end_dvm,
};
