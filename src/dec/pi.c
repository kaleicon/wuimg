// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/pi.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct pi_desc *desc = ptr;
	if (desc->comm.str) {
		tree_add_leaf_len(tree, "Comment", wuptr_wustr(desc->comm),
			"SHIFT-JIS");
	}
	tree_add_leaf_len(tree, "Saver model", WUPTR_ARRAY(desc->saver.sig),
		"SHIFT-JIS");
	if (desc->saver.data) {
		tree_add_leaf_len(tree, "Saver data",
			wuptr_mem(desc->saver.data, desc->saver.len), "SHIFT-JIS");
	}
	tree_bud_leaf_u(tree, "Depth", desc->depth);
}

static void cleanup(void *ptr) {
	pi_cleanup(ptr);
}
static size_t dec(const void *restrict ptr, struct wuimg *img) {
	return pi_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct wuimg *img) {
	return pi_read_header(ptr, img);
}
static enum wu_error open(void *restrict ptr, struct image_file *infile) {
	return pi_open_file(ptr, infile->ifp);
}

enum wu_error pi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pi_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, cleanup);
}

const struct image_fn pi_fn = {.dec = pi_dec};
