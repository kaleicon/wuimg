#include <string.h>

#include "../wudefs.h"
#include "../wutree.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/tim.h"

static void read_metadata(struct wu_tree *tree, const struct tim_desc *desc) {
	struct wu_tree_sap sap[3] = {
		{"Depth", wu_leaf_unsigned, {.u = desc->r.ch * desc->r.bitdepth}},
		{"X", wu_leaf_unsigned, {.u = desc->x}},
		{"Y", wu_leaf_unsigned, {.u = desc->y}},
	};
	tree_bud_leaves(tree, sap, 1);

	struct wu_tree *offset = tree_sprout_branch(tree, "Offset");
	if (offset) {
		tree_bud_leaves(offset, sap + 1, 2);
	}

	if (desc->clut.data) {
		struct wu_tree *pal = tree_sprout_branch(tree, "CLUT");
		if (pal) {
			const struct tim_clut *clut = &desc->clut;
			sap[0].name = "Number";
			sap[0].val.u = clut->nb;
			sap[1].val.u = clut->x;
			sap[2].val.u = clut->y;
			tree_bud_leaves(pal, sap, 3);
		}
	}
}

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, struct tim_desc *desc) {
	const enum lib_fail status = tim_parse_header(desc);
	if (status) {
		rast_error(infile, status);
		return wu_invalid_header;
	}

	read_metadata(&infile->metadata, desc);

	if (rast_exceeds_size(&desc->r, wuconf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	if (!rast_to_raw_img(&desc->r, img)) {
		return wu_alloc_error;
	}
	return tim_decode(desc, img->data) ? wu_ok : wu_decoding_error;
}

enum wu_error tim_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct tim_desc desc;
	const enum lib_fail fail = tim_open_file(&desc, infile->ifp);
	if (fail == lib_ok) {
		const enum wu_error s = decode(infile, wuconf, &desc);
		tim_cleanup(&desc);
		return s;
	}
	rast_error(infile, fail);
	return wu_open_error;
}
