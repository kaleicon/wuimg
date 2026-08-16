// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/mag.h"

static void get_mag_metadata(const struct mag_desc *desc, struct wutree *tree) {
	tree_add_leaf_len(tree, "Model", WUPTR_ARRAY(desc->model), "SHIFT-JIS");
	tree_add_leaf_utf8(tree, "Code", mag_model_code_str(desc->code));
	if (desc->code == mag_model_msx) {
		tree_add_leaf_utf8(tree, "MSX Screen mode",
			mag_msx_screen_str(desc->msx.screen));
		tree_bud_leaf_bool(tree, "Interlace", desc->msx.interlace);
	}
	tree_add_leaf_utf8(tree, "Screen mode",
		mag_screen_mode_str(desc->screen_mode));
	tree_add_leaf_len(tree, "Comment", desc->comm, "SHIFT_JIS");
	tree_add_leaf_len(tree, "Dummy", desc->dummy, NULL);
}

static void end_mag(struct image_file *infile) {
	mag_cleanup(infile->dec_state);
}

static struct wu_st event_mag(struct image_file *infile, struct wu_state *state,
const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		get_mag_metadata(infile->dec_state, &infile->metadata);
		return WU_OK;
	case ev_subcycle:
		mag_decode(infile->dec_state, infile->sub_img);
		return WU_OK;
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_mag(struct image_file *infile) {
	return mag_parse(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn mag_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct mag_desc),
	.init = init_mag,
	.event = event_mag,
	.end = end_mag,
};
