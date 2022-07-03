#include <stdlib.h>
#include <string.h>

#include "../wudefs.h"
#include "../lib/tim.h"

static void read_metadata(struct wu_tree *tree, const struct tim_desc *desc,
const struct raw_img *img) {
	struct wu_tree *offset = tree_sprout_branch(tree, "Offset");
	if (offset) {
		struct wu_tree_sap sap[] = {
			{"X", {wu_leaf_unsigned, {.u = desc->x}}},
			{"Y", {wu_leaf_unsigned, {.u = desc->y}}},
		};
		tree_bud_leaves(offset, sap, ARRAY_LEN(sap));
	}

	if (img->mode == image_mode_palette) {
		struct wu_tree *pal = tree_sprout_branch(tree, "CLUT");
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

enum wu_error tim_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct tim_desc desc;
	enum wu_error st = tim_open_file(&desc, infile->ifp);
	if (st != wu_ok) {
		return st;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	st = tim_parse_header(&desc, img);
	if (st != wu_ok) {
		return st;
	}

	read_metadata(&infile->metadata, &desc, img);

	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	return tim_decode(&desc, img) ? wu_ok : wu_decoding_error;
}
