// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/txf.h"

static struct wu_st init_txf(struct image_file *infile) {
	struct txf_desc desc;
	struct wu_st st = txf_parse(&desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		const enum wu_error e = wuimg_alloc_limit(infile->sub_img,
			infile->conf);
		if (e == wu_ok) {
			tree_bud_leaf_u(&infile->metadata, "Max ascent",
				desc.max_ascent);
			tree_bud_leaf_u(&infile->metadata, "Max descent",
				desc.max_descent);
			st = txf_load(&desc, infile->sub_img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}

const struct image_fn txf_fn = {
	.alloc_single = true,
	.init = init_txf,
};
