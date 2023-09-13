// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/pic.h"
#include "misc/common.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct pic_desc *desc = ptr;
	if (desc->comm.str) {
		tree_add_leaf_len(tree, "Comment", wuptr_wustr(desc->comm),
			"SHIFT-JIS");
	}
	tree_add_leaf_utf8(tree, "Model", pic_model_str(desc->type));
	tree_bud_leaf_u(tree, "Mode", desc->mode);
	tree_bud_leaf_u(tree, "Depth", desc->depth);
	tree_bud_leaf_d(tree, "X", desc->x);
	tree_bud_leaf_d(tree, "Y", desc->y);
	if (desc->tiled) {
		tree_bud_leaf_bool(tree, "Tiled", true);
	}
}

static void cleanup(void *ptr) {
	pic_cleanup(ptr);
}
static size_t dec(const void *restrict ptr, struct wuimg *img) {
	return pic_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct wuimg *img) {
	return pic_parse(ptr, img);
}
static enum wu_error open(void *restrict ptr, struct image_file *infile) {
	return pic_open(ptr, infile->ifp);
}

enum wu_error pic_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pic_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, cleanup);
}

const struct image_fn pic_fn = {.dec = pic_dec};
