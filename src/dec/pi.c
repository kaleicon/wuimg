#include <string.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/pi.h"

static void read_metadata(struct wu_tree *tree, const struct pi_desc *desc) {
	if (desc->comment.data) {
		size_t len;
		if (desc->comment.area_len > desc->comment.text_len + 1) {
			len = desc->comment.area_len;
		} else {
			len = desc->comment.text_len;
		}
		tree_sprout_unsafe_leaf(tree, "Comment", desc->comment.data, len);
	}
	tree_sprout_unsafe_leaf(tree, "Saver model", desc->saver.sig,
		sizeof(desc->saver.sig));
	if (desc->saver.data) {
		tree_sprout_unsafe_leaf(tree, "Saver data", desc->saver.data,
			desc->saver.len);
	}
	tree_bud_leaf(tree, "Depth",
		(struct wu_leaf){.val.u = desc->depth, .type = wu_leaf_unsigned});
}

enum wu_error pi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pi_desc desc;
	enum lib_fail fail = pi_open_file(infile->ifp, &desc);
	if (fail) {
		rast_error(infile, fail);
		return wu_invalid_signature;
	}

	fail = pi_read_header(&desc);
	if (fail) {
		pi_cleanup(&desc);
		rast_error(infile, fail);
		return wu_invalid_header;
	}

	read_metadata(&infile->metadata, &desc);

	if (rast_exceeds_size(&desc.rast, wuconf)) {
		pi_cleanup(&desc);
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		pi_cleanup(&desc);
		return wu_alloc_error;
	}

	img->data = pi_decode(&desc);
	rast_to_raw(img, &desc.rast);
	pi_cleanup(&desc);
	if (!img->data) {
		return wu_alloc_error;
	}
	return wu_ok;
}
