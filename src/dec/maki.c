// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/maki.h"

static struct wu_st init_maki(struct image_file *infile,
const struct wu_conf *conf) {
	struct maki_desc desc;
	struct wu_st st = maki_parse(&desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(infile->sub_img, conf);
		if (e == wu_ok) {
			struct wutree *tree = &infile->metadata;
			tree_add_leaf_utf8(tree, "Version",
				maki_version_str(desc.version));
			tree_add_leaf_len(tree, "Model",
				WUPTR_ARRAY(desc.model), "SHIFT-JIS");
			tree_add_leaf_len(tree, "Comment",
				WUPTR_ARRAY(desc.comment), "SHIFT-JIS");
			tree_bud_leaf_u(tree, "X", desc.x);
			tree_bud_leaf_u(tree, "Y", desc.y);
			st = maki_decode(&desc, infile->sub_img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}

const struct image_fn maki_fn = {
	.alloc_single = true,
	.init = init_maki,
};
