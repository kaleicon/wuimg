// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/tlg.h"

static struct wu_st event_tlg(struct image_file *infile,
struct wu_state *state, enum image_event ev) {
	(void)state;
	struct tlg_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_add_leaf_utf8(&infile->metadata, "Version",
			tlg_version_str(desc->version));
		return WU_OK;
	case ev_subcycle:
		return tlg_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_tlg(struct image_file *infile) {
	return tlg_read_header(infile->dec_state, infile->map, infile->sub_img);
}

const struct image_fn tlg_fn = {
	.mmap = true,
	.state_size = sizeof(struct tlg_desc),
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.init = init_tlg,
	.event = event_tlg,
};
