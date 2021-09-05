#include <string.h>

#include "../wudefs.h"
#include "../wutree.h"
#include "../common.h"
#include "../lib/tim.h"

void add_metadata(struct wu_tree *tree, const struct tim_desc *desc) {
	struct wu_tree_sap sap[] = {
		{"Bitdepth", wu_leaf_unsigned, {.u = desc->bitdepth}},
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

	if (zumax(desc.w, desc.h) > wuconf->max_img_size) {
		tim_cleanup(&desc);
		return wu_exceeds_size_limit;
	}

	add_metadata(&infile->metadata, &desc);

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		tim_cleanup(&desc);
		return wu_alloc_error;
	}

	img->palette = tim_take_colormap(&desc);
	img->data = tim_decode(&desc);
	tim_cleanup(&desc);
	if (!img->data) {
		return wu_decoding_error;
	}
	img->w = desc.w;
	img->h = desc.h;
	img->channels = desc.bitdepth > 8 ? 3 : 1;
	if (desc.bitdepth == 16) { //rgb555
		img->bitdepth = 8;
	} else {
		img->bitdepth = (unsigned char)imin(desc.bitdepth, 8);
	}
	img->alignment = 2;
	return wu_ok;
}
