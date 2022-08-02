#include <stdlib.h>
#include <string.h>

#include "wudefs.h"
#include "rast_utils.h"
#include "lib/tim.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct tim_desc *desc = ptr;
	struct wu_tree *offset = tree_add_branch(tree, "Offset");
	if (offset) {
		struct wu_tree_sap sap[] = {
			{"X", {wu_leaf_unsigned, {.u = desc->x}}},
			{"Y", {wu_leaf_unsigned, {.u = desc->y}}},
		};
		tree_bud_leaves(offset, sap, ARRAY_LEN(sap));
	}

	if (desc->clut.nb) {
		struct wu_tree *pal = tree_add_branch(tree, "CLUT");
		if (pal) {
			struct wu_tree_sap sap[] = {
				{"Nb.", {wu_leaf_unsigned, {.u = desc->clut.nb}}},
				{"X", {wu_leaf_unsigned, {.u = desc->clut.x}}},
				{"Y", {wu_leaf_unsigned, {.u = desc->clut.y}}},
			};
			tree_bud_leaves(pal, sap, ARRAY_LEN(sap));
		}
	}
}

static size_t dec(const void *restrict ptr, struct raw_img *img) {
	return tim_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct raw_img *img) {
	return tim_parse_header(ptr, img);
}
static enum wu_error open(void *restrict ptr, FILE *ifp) {
	return tim_open_file(ptr, ifp);
}

enum wu_error tim_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct tim_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, NULL);
}
