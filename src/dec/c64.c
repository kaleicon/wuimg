// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2023 kaleido
#include "lib/c64.h"
#include "wudefs.h"

static void get_c64_meta(const struct c64_desc *desc, struct wutree *tree) {
	tree_add_leaf_utf8(tree, "Type", c64_fmt_str(desc->fmt));
	tree_add_leaf_utf8(tree, "Mode", c64_mode_str(desc->info.mode));
	tree_bud_leaf_bool(tree, "FLI", desc->info.fli);
}

static struct wu_st event_c64(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		;struct wu_st st = c64_set(infile->dec_state, infile->sub_img);
		if (wu_isok(st)) {
			get_c64_meta(infile->dec_state, &infile->metadata);
		}
		return st;
	case ev_subcycle:
		return c64_decode(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_c64(struct image_file *infile) {
	return c64_guess(infile->dec_state, infile->map, infile->ext);
}

const struct image_fn c64_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct c64_desc),
	.init = init_c64,
	.event = event_c64,
};
