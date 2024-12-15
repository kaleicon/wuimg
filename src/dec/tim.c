// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/tim.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct tim_desc *desc = ptr;
	struct wu_tree *offset = tree_add_branch(tree, "Offset");
	if (offset) {
		tree_bud_leaf_u(offset, "X", desc->x);
		tree_bud_leaf_u(offset, "Y", desc->y);
	}

	if (desc->clut.nb) {
		struct wu_tree *pal = tree_add_branch(tree, "CLUT");
		if (pal) {
			tree_bud_leaf_u(pal, "Nb.", desc->clut.nb);
			tree_bud_leaf_u(pal, "X", desc->clut.x);
			tree_bud_leaf_u(pal, "Y", desc->clut.y);
		}
	}
}

static size_t dec(const void *restrict ptr, struct wuimg *img) {
	return tim_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct wuimg *img) {
	return tim_parse_header(ptr, img);
}
static enum wu_error open(void *restrict ptr, struct image_file *infile) {
	return tim_open_file(ptr, infile->ifp);
}

static enum wu_error tim_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct tim_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec);
}

const struct image_fn tim_fn = {.dec = tim_dec};
