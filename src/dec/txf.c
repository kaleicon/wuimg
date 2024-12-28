// SPDX-License-Identifier: 0BSD
#include "lib/txf.h"
#include "rast_utils.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct txf_desc *desc = ptr;
	tree_bud_leaf_u(tree, "Max ascent", desc->max_ascent);
	tree_bud_leaf_u(tree, "Max descent", desc->max_descent);
}
static size_t dec(const void *restrict desc, struct wuimg *img) {
	return txf_load(desc, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return txf_parse(desc, img);
}
static enum wu_error init(void *restrict desc, struct image_file *infile) {
	return txf_init(desc, infile->ifp);
}

static enum wu_error setup(struct image_file *infile,
const struct wu_conf *conf) {
	struct txf_desc desc;
	return rast_trivial_dec(infile, conf, &desc, init, parse, metadata, dec);
}

const struct image_fn txf_fn = {
	.dec = setup,
};
