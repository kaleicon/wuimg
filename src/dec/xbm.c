#include <stdio.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"

#include "../lib/xbm.h"

static void get_metadata(struct wu_tree *tree, const struct xbm_desc *desc) {
	struct wu_leaf leaf = {
		.type = wu_leaf_unsigned,
		.val = {.u = (desc->type == xbm_x11) ? 11 : 10},
	};
	tree_bud_leaf(tree, "Version", leaf);
	if (desc->name.len) {
		tree_sprout_unsafe_leaf(tree, "Source name", desc->name.str,
			desc->name.len);
	}
	if (desc->comment.len) {
		tree_sprout_unsafe_leaf(tree, "Comment", desc->comment.str,
			desc->comment.len);
	}
	if (desc->has_hotspot) {
		struct wu_tree *hot = tree_sprout_branch(tree, "Hot spot");

		leaf.type = wu_leaf_unsigned;
		leaf.val.u = desc->x_hot;
		tree_bud_leaf(hot, "X", leaf);
		leaf.val.u = desc->y_hot;
		tree_bud_leaf(hot, "Y", leaf);
	}
}

enum wu_error xbm_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct mmap_info mm;
	if (!mmap_file(&mm, infile->ifp)) {
		return wu_alloc_error;
	}

	struct xbm_desc desc;
	enum lib_fail fail = xbm_open_mem(&desc, &mm);
	if (fail) {
		munmap_file(mm);
		rast_error(infile, fail);
		return wu_unknown_file_type;
	}

	get_metadata(&infile->metadata, &desc);

	if (rast_exceeds_size(&desc.r, wuconf)) {
		munmap_file(mm);
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		munmap_file(mm);
		return wu_alloc_error;
	}

	rast_to_raw(img, &desc.r);
	img->data = xbm_decode(&desc);
	munmap_file(mm);
	if (!img->data) {
		return wu_decoding_error;
	}
	return wu_ok;
}
