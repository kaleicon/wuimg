// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/c.h"

static void get_c_metadata(struct wutree *tree, const struct c_desc *desc) {
	if (desc->fmt == c_xbm) {
		tree_add_leaf_utf8(tree, "Type",
			(desc->xbm.version == xbm_x11) ? "XBM X11" : "XBM X10");
		if (desc->xbm.has_hotspot) {
			struct wutree *hot = tree_add_branch(tree, "Hot spot");
			if (hot) {
				tree_bud_leaf_d(hot, "X", desc->xbm.x_hot);
				tree_bud_leaf_d(hot, "Y", desc->xbm.y_hot);
			}
		}
	} else {
		tree_add_leaf_utf8(tree, "Type", "DEGAS Elite Icon");
	}
}

static struct wu_st dec_c(struct image_file *infile) {
	struct c_desc desc;
	struct wuimg *img = infile->sub_img;
	struct wu_st st = c_parse(&desc, img, infile->map);
	if (wu_isok(st)) {
		st = WUERR_CHECK(wuimg_alloc_limit(img, infile->conf));
		if (wu_isok(st)) {
			get_c_metadata(&infile->metadata, &desc);
			st = c_decode(&desc, img);
		}
	}
	return st;
}

const struct image_fn c_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = dec_c,
};
