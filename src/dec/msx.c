// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "lib/msx.h"
#include "raster/fmt.h"
#include "wudefs.h"

static struct wu_st event_msx(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct msx_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		if (msx_mode_may_be_compressed(desc->mode)) {
			tree_bud_leaf_bool(&infile->metadata,
				"Compressed", desc->compressed);
		}
		if (msx_mode_may_have_alternate_field(desc->mode)) {
			tree_bud_leaf_bool(&infile->metadata,
				"Is alternate field",
				desc->mod == msx_mod_alt_field);
		}
		if (desc->mod == msx_mod_graph_saurus) {
			tree_bud_leaf_bool(&infile->metadata,
				"External palette", desc->external_palette);
		}
		return WU_OK;
	case ev_subcycle:
		return msx_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_msx(struct image_file *infile) {
	return msx_parse(infile->dec_state, infile->sub_img, infile->ifp,
		infile->name, infile->ext);
}


static struct wu_st event_msxgl(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		return msxgl_parse(infile->sub_img, infile->ifp, infile->name,
			infile->ext);
	case ev_subcycle:
		return fmt_load_raster_st(infile->sub_img, infile->ifp);
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn msx_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct msx_desc),
	.init = init_msx,
	.event = event_msx,
};
const struct image_fn msxgl_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.event = event_msxgl,
};
