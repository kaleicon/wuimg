// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include "lib/sixel.h"
#include "wudefs.h"

static struct wu_st init_sixel(struct image_file *infile) {
	struct sixel_desc desc;
	struct wu_st st = sixel_try_parse(&desc, infile->sub_img, infile->map,
		4096);
	if (wu_isok(st)) {
		tree_bud_leaf_u(&infile->metadata, "Horizontal grid size",
			desc.horizontal_grid_size);
		enum wu_error err = wuimg_alloc_limit(infile->sub_img,
			infile->conf);
		if (err == wu_ok) {
			st = sixel_decode(&desc, infile->sub_img);
		} else {
			st = WUERR_HERE(err);
		}
	}
	return st;
}

const struct image_fn sixel_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_sixel,
};
