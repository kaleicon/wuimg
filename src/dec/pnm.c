// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include "wudefs.h"
#include "lib/pnm.h"

static struct wu_st event_pnm(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	const size_t i = (size_t)state->idx;
	struct wuimg *img = infile->sub_img + i;
	switch (ev) {
	case ev_metadata: return pnm_get_info(infile->dec_state, img);
	case ev_subcycle: return pnm_get_raster(infile->dec_state, img, i);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_pnm(struct image_file *infile) {
	struct pnm_desc *desc = infile->dec_state;
	struct wu_st st = pnm_parse(desc, infile->ifp, true);
	if (wu_isok(st)) {
		tree_add_leaf_utf8(&infile->metadata, "Type",
			pnm_type_str(desc->type));
		infile->nr = desc->nr;
	}
	return st;
}

const struct image_fn pnm_fn = {
	.state_size = sizeof(struct pnm_desc),
	.alloc_on_subcycle = true,
	.init = init_pnm,
	.event = event_pnm,
};
