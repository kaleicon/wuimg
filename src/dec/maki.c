#include "rast_utils.h"
#include "lib/maki.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct maki_desc *desc = ptr;
	tree_add_leaf_utf8(tree, "Version", maki_version_str(desc->version));
	tree_add_leaf_len(tree, "Model", desc->model, sizeof(desc->model),
		"SHIFT-JIS");
	tree_add_leaf_len(tree, "Comment", desc->comment,
		sizeof(desc->comment), "SHIFT-JIS");
	struct wu_leaf leaf = {.type = wu_leaf_unsigned, .val.u = desc->x};
	tree_bud_leaf(tree, "X", leaf);
	leaf.val.u = desc->y;
	tree_bud_leaf(tree, "Y", leaf);
}

static size_t dec(const void *restrict desc, struct wuimg *img) {
	return maki_decode(desc, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return maki_parse(desc, img);
}
static enum wu_error open(void *restrict desc, FILE *ifp) {
	return maki_open(desc, ifp);
}

enum wu_error maki_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct maki_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc,
		open, parse, metadata, dec, NULL);
}
