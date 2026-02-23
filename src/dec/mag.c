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

static struct wu_st init_mag(struct image_file *infile) {
	struct mag_desc desc;
	struct wu_st st = mag_parse(&desc, infile->sub_img, infile->map);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(infile->sub_img, infile->conf);
		if (e == wu_ok) {
			get_mag_metadata(&desc, &infile->metadata);
			mag_decode(&desc, infile->sub_img);
		} else {
			st = WUERR_HERE(e);
		}
		mag_cleanup(&desc);
	}
	return st;
}

const struct image_fn mag_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_mag,
};
