#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"

#include "../lib/bmp.h"

static void get_bmp_metadata(struct wu_tree *tree, const struct bmp_desc *desc) {
	tree_sprout_leaf(tree, "Header", bmp_type_str(desc->type));
	tree_sprout_leaf(tree, "Compression", bmp_compression_str(desc->compression));

	struct wu_leaf leaf = {.val.u = desc->depth, .type = wu_leaf_unsigned};
	tree_bud_leaf(tree, "Depth", leaf);
}

enum wu_error bmp_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct bmp_desc desc;
	enum lib_fail fail = bmp_open_file(infile->ifp, &desc);
	if (fail) {
		return wu_open_error;
	}

	fail = bmp_parse_header(&desc);
	if (fail) {
		bmp_cleanup(&desc);
		return wu_invalid_header;
	}

	if (rast_exceeds_size(&desc.r, wuconf)) {
		bmp_cleanup(&desc);
		return wu_exceeds_size_limit;
	}

	get_bmp_metadata(&infile->metadata, &desc);

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		bmp_cleanup(&desc);
		return wu_alloc_error;
	}

	rast_to_raw(img, &desc.r);
	img->mirror = (desc.order == bmp_bottom_up);
	if (desc.compression == bmp_no_compression) {
		if (desc.depth == 16) {// || desc.depth == 32) {
			img->no_alpha = true;
		}
	}

	img->data = bmp_decode(&desc);
	bmp_cleanup(&desc);
	return img->data ? wu_ok : wu_decoding_error;
}
