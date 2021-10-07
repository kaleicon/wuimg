#include <string.h>

#include "../wudefs.h"
#include "../wutree.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/tim.h"

static void read_metadata(struct wu_tree *tree, const struct tim_desc *desc) {
	struct wu_tree_sap sap[] = {
		{"Bitdepth", wu_leaf_unsigned, {.u = desc->r.ch * desc->r.bitdepth}},
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
			sap[0].value.u = clut->nb;
			sap[1].value.u = clut->x;
			sap[2].value.u = clut->y;
			tree_bud_leaves(pal, sap, 3);
		}
	}
}

enum wu_error tim_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct tim_desc desc;
	enum lib_fail status = tim_open_file(infile->ifp, &desc);
	if (status != lib_ok) {
		infile->err_msg = strdup(lib_fail_string(status));
		return wu_open_error;
	}

	status = tim_parse_header(&desc);
	if (status) {
		tim_cleanup(&desc);
		infile->err_msg = strdup(lib_fail_string(status));
		return wu_invalid_header;
	}

	if (rast_exceeds_size(&desc.r, wuconf)) {
		tim_cleanup(&desc);
		return wu_exceeds_size_limit;
	}

	read_metadata(&infile->metadata, &desc);

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		tim_cleanup(&desc);
		return wu_alloc_error;
	}

	rast_to_raw(img, &desc.r);
	img->data = tim_decode(&desc);
	tim_cleanup(&desc);
	return (img->data) ? wu_ok : wu_decoding_error;
}
