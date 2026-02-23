// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/prt.h"

static void end_prt(struct image_file *infile) {
	prt_cleanup(infile->dec_state);
}

static struct wu_st init_prt(struct image_file *infile) {
	struct prt_desc *desc = infile->dec_state;
	struct wu_st st = prt_parse(desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(infile->sub_img,
			infile->conf);
		if (e == wu_ok) {
			struct wutree *tree = &infile->metadata;
			tree_bud_leaf_u(tree, "Version", desc->version);
			tree_bud_leaf_u(tree, "Depth", desc->depth);
			tree_bud_leaf_bool(tree, "Mask", desc->mask);
			if (desc->version == prt_v102) {
				tree_bud_leaf_u(tree, "X", desc->x);
				tree_bud_leaf_u(tree, "Y", desc->y);
			}
			st = prt_decode(desc, infile->sub_img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}

const struct image_fn prt_fn = {
	.alloc_single = true,
	.state_size = sizeof(struct prt_desc),
	.init = init_prt,
	.end = end_prt,
};
