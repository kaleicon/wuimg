// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "misc/common.h"
#include "lib/prt.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct prt_desc *desc = ptr;
	tree_bud_leaf_u(tree, "Version", desc->version);
	tree_bud_leaf_u(tree, "Depth", desc->depth);
	tree_bud_leaf_bool(tree, "Mask", desc->mask);
	if (desc->version == prt_v102) {
		tree_bud_leaf_u(tree, "X", desc->x);
		tree_bud_leaf_u(tree, "Y", desc->y);
	}
}

static void cleanup(void *ptr) {
	prt_cleanup(ptr);
}
static size_t dec(const void *restrict ptr, struct wuimg *img) {
	return prt_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct wuimg *img) {
	return prt_parse(ptr, img);
}
static enum wu_error open(void *restrict ptr, struct image_file *infile) {
	return prt_open(ptr, infile->ifp);
}

static enum wu_error prt_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct prt_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, cleanup);
}

const struct image_fn prt_fn = {.dec = prt_dec};
