#include "rast_utils.h"
#include "lib/pic.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct pic_desc *desc = ptr;
	if (desc->comm.str) {
		tree_add_leaf_len(tree, "Comment", desc->comm.str,
			desc->comm.len, "SHIFT-JIS");
	}
	tree_add_leaf_utf8(tree, "Model", pic_model_str(desc->type));
	tree_bud_leaf(tree, "Mode", (struct wu_leaf){.val.u = desc->mode,
		.type = wu_leaf_unsigned});
	tree_bud_leaf(tree, "Depth", (struct wu_leaf){.val.u = desc->depth,
		.type = wu_leaf_unsigned});
	if (desc->x > 0 || desc->y > 0) {
		tree_bud_leaf(tree, "X", (struct wu_leaf){.val.d = desc->x,
			.type = wu_leaf_signed});
		tree_bud_leaf(tree, "Y", (struct wu_leaf){.val.d = desc->x,
			.type = wu_leaf_signed});
	}
	if (desc->tiled) {
		tree_bud_leaf(tree, "Tiled", (struct wu_leaf){.val.b = true,
			.type = wu_leaf_bool});
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
static enum wu_error open(void *restrict ptr, FILE *ifp) {
	return pic_open(ptr, ifp);
}

enum wu_error pic_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pic_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, cleanup);
}
