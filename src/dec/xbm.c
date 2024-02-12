// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/xbm.h"

static void get_metadata(struct wu_tree *tree, const struct xbm_desc *desc) {
	tree_bud_leaf_u(tree, "Version", (desc->type == xbm_x11) ? 11 : 10);
	if (desc->name.len) {
		tree_add_leaf_len(tree, "Source name", desc->name, NULL);
	}
	if (desc->comment.len) {
		tree_add_leaf_len(tree, "Comment", desc->comment, NULL);
	}
	if (desc->has_hotspot) {
		struct wu_tree *hot = tree_add_branch(tree, "Hot spot");
		if (hot) {
			tree_bud_leaf_d(hot, "X", desc->x_hot);
			tree_bud_leaf_d(hot, "Y", desc->y_hot);
		}
	}
}

static enum wu_error xbm_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct wuimg *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	struct xbm_desc desc;
	const enum wu_error st = xbm_parse_header(&desc, img,
		mp_map(infile->map));
	if (st) {
		return st;
	}

	get_metadata(&infile->metadata, &desc);
	if (wuimg_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	return xbm_decode(&desc, img) ? wu_ok : wu_decoding_error;
}

const struct image_fn xbm_fn = {.mmap = true, .dec = xbm_dec};
