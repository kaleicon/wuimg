// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/maki.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct maki_desc *desc = ptr;
	tree_add_leaf_utf8(tree, "Version", maki_version_str(desc->version));
	tree_add_leaf_len(tree, "Model", WUPTR_ARRAY(desc->model), "SHIFT-JIS");
	tree_add_leaf_len(tree, "Comment", WUPTR_ARRAY(desc->comment),
		"SHIFT-JIS");
	tree_bud_leaf_u(tree, "X", desc->x);
	tree_bud_leaf_u(tree, "Y", desc->y);
}

static size_t dec(const void *restrict desc, struct wuimg *img) {
	return maki_decode(desc, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return maki_parse(desc, img);
}
static enum wu_error open(void *restrict desc, struct image_file *infile) {
	return maki_open(desc, infile->ifp);
}

static enum wu_error maki_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct maki_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, NULL);
}

const struct image_fn maki_fn = {.dec = maki_dec};
