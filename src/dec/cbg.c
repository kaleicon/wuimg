// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/cbg.h"

static struct wu_st init_cbg(struct image_file *infile) {
	struct cbg_desc desc;
	struct wu_st st = cbg_parse(&desc, infile->sub_img, infile->map);
	if (wu_isok(st)) {
		enum wu_error err = wuimg_alloc_limit(infile->sub_img, infile->conf);
		if (err == wu_ok) {
			tree_bud_leaf_u(&infile->metadata, "Version",
				desc.version);
			st = cbg_decode(&desc, infile->sub_img);
		} else {
			st = WUERR_HERE(err);
		}
	}
	return st;
}

const struct image_fn cbg_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_cbg,
};
