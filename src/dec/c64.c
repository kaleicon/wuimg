// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2023 kaleido
#include "lib/c64.h"
#include "wudefs.h"

static void get_meta(const struct c64_desc *desc, struct wutree *tree) {
	tree_add_leaf_utf8(tree, "Type", c64_fmt_str(desc->fmt));
	tree_add_leaf_utf8(tree, "Mode", c64_mode_str(desc->info.mode));
}

static struct wu_st dec_c64(struct image_file *infile,
const struct wu_conf *conf) {
	struct c64_desc desc;
	struct wu_st st = c64_guess(&desc, infile->map, infile->ext);
	if (wu_isok(st)) {
		st = c64_set(&desc, infile->sub_img);
		if (wu_isok(st)) {
			if (wuimg_exceeds_limit(infile->sub_img, conf)) {
				st = WUERR_HERE(wu_exceeds_size_limit);
			} else {
				get_meta(&desc, &infile->metadata);
				st = c64_decode(&desc, infile->sub_img);
			}
		}
	}
	return st;
}

const struct image_fn c64_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = dec_c64,
};
