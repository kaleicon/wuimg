// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/pdt.h"

static void cleanup_pdt(struct image_file *infile) {
	pdt_cleanup(infile->dec_state);
}

static struct wu_st event_pdt(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct pdt_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_add_leaf_utf8(&infile->metadata, "Version",
			pdt_version_str(desc->version));
		return WU_OK;
	case ev_subcycle:
		return pdt_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_pdt(struct image_file *infile) {
	return pdt_init(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn pdt_fn = {
	.mmap = true,
	.state_size = sizeof(struct pdt_desc),
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.init = init_pdt,
	.event = event_pdt,
	.end = cleanup_pdt,
};
