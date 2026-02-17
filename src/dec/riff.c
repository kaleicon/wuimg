// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include "lib/riff.h"
#include "wudefs.h"

static struct wu_st event_riffpal(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf; (void)state;
	struct riffpal_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_u(&infile->metadata, "Entries", desc->entries);
		return WU_OK;
	case ev_subcycle:
		return riffpal_load(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_riffpal(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	return riffpal_init(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn riffpal_fn = {
	.state_size = sizeof(struct riffpal_desc),
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.init = init_riffpal,
	.event = event_riffpal,
};
