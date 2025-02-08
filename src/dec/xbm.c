// SPDX-License-Identifier: 0BSD
#include "wudefs.h"
#include "lib/xbm.h"

static void get_metadata(struct wutree *tree, const struct xbm_desc *desc) {
	tree_bud_leaf_u(tree, "Version", (desc->type == xbm_x11) ? 11 : 10);
	tree_add_leaf_len(tree, "Source name", desc->name, NULL);
	tree_add_leaf_len(tree, "Comment", desc->comment, NULL);
	if (desc->has_hotspot) {
		struct wutree *hot = tree_add_branch(tree, "Hot spot");
		if (hot) {
			tree_bud_leaf_d(hot, "X", desc->x_hot);
			tree_bud_leaf_d(hot, "Y", desc->y_hot);
		}
	}
}

static enum wu_error xbm_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct xbm_desc desc;
	struct wuimg *img = infile->sub_img;
	const enum wu_error st = xbm_parse_header(&desc, img, infile->map);
	if (st) {
		return st;
	}

	get_metadata(&infile->metadata, &desc);
	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	return xbm_decode(&desc, img) ? wu_ok : wu_decoding_error;
}

const struct image_fn xbm_fn = {
	.mmap = true,
	.alloc_single = true,
	.dec = xbm_dec,
};
