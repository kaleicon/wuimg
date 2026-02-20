// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/cisrle.h"
#include "wudefs.h"

static struct wu_st event_cis(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf; (void)state;
	struct cis_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_add_leaf_utf8(&infile->metadata, "Resolution",
			cis_resolution_str(desc->res));
		return WU_OK;
	case ev_subcycle:
		return cis_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_cis(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	return cis_parse(infile->dec_state, infile->sub_img, infile->map, true);
}

const struct image_fn cisrle_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct cis_desc),
	.init = init_cis,
	.event = event_cis,
};
