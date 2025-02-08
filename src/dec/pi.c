// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/pi.h"

static void metadata(const void *restrict ptr, struct wutree *tree) {
	const struct pi_desc *desc = ptr;
	tree_add_leaf_len(tree, "Comment", desc->comm, "SHIFT-JIS");
	tree_add_leaf_len(tree, "Dummy", desc->dummy, NULL);
	tree_add_leaf_len(tree, "Saver model", WUPTR_ARRAY(desc->saver.model),
		"SHIFT-JIS");
	tree_add_leaf_len(tree, "Saver data", desc->saver.data, "SHIFT-JIS");
	tree_bud_leaf_u(tree, "Depth", desc->depth);
}

static size_t dec(const void *restrict ptr, struct wuimg *img) {
	return pi_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct wuimg *img) {
	return pi_read_header(ptr, img);
}
static enum wu_error open(void *restrict ptr, struct image_file *infile) {
	return pi_init(ptr, infile->map);
}

static enum wu_error pi_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct pi_desc desc;
	return rast_trivial_dec(infile, conf, &desc, open, parse, metadata,
		dec);
}

const struct image_fn pi_fn = {
	.mmap = true,
	.dec = pi_dec,
};
