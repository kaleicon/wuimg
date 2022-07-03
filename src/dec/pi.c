#include <string.h>

#include "../wudefs.h"
#include "../common.h"
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

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, struct pi_desc *desc) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	const enum wu_error status = pi_read_header(desc, img);
	if (status != wu_ok) {
		return status;
	}

	read_metadata(&infile->metadata, desc);

	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	return pi_decode(desc, img) ? wu_ok : wu_decoding_error;
}

enum wu_error pi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pi_desc desc;
	enum wu_error err = pi_open_file(&desc, infile->ifp);
	if (err == wu_ok) {
		err = dec_wrap(infile, wuconf, &desc);
		pi_cleanup(&desc);
	}
	return err;
}
