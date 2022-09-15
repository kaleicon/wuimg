#include "rast_utils.h"
#include "lib/xbm.h"

static void get_metadata(struct wu_tree *tree, const struct xbm_desc *desc) {
	struct wu_leaf leaf = {
		.type = wu_leaf_unsigned,
		.val = {.u = (desc->type == xbm_x11) ? 11 : 10},
	};
	tree_bud_leaf(tree, "Version", leaf);
	if (desc->name.len) {
		tree_add_leaf_len(tree, "Source name", desc->name.ptr,
			desc->name.len, NULL);
	}
	if (desc->comment.len) {
		tree_add_leaf_len(tree, "Comment", desc->comment.ptr,
			desc->comment.len, NULL);
	}
	if (desc->has_hotspot) {
		struct wu_tree *hot = tree_add_branch(tree, "Hot spot");

		leaf.type = wu_leaf_signed;
		leaf.val.d = desc->x_hot;
		tree_bud_leaf(hot, "X", leaf);
		leaf.val.d = desc->y_hot;
		tree_bud_leaf(hot, "Y", leaf);
	}
}

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	struct xbm_desc desc;
	const enum wu_error st = xbm_parse_header(&desc, img,
		mp_parser_mem(mm->len, mm->data));
	if (st) {
		return st;
	}

	get_metadata(&infile->metadata, &desc);

	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	return xbm_decode(&desc, img) ? wu_ok : wu_decoding_error;
}

enum wu_error xbm_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return rast_map_wrap(infile, wuconf, decode);
}
