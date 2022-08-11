#include <string.h>

#include "wudefs.h"
#include "rast_utils.h"
#include "lib/pi.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct pi_desc *desc = ptr;
	if (desc->comment.data) {
		size_t len;
		if (desc->comment.area_len > desc->comment.text_len + 1) {
			len = desc->comment.area_len;
		} else {
			len = desc->comment.text_len;
		}
		tree_add_leaf_len(tree, "Comment", desc->comment.data, len,
			"SHIFT-JIS");
	}
	tree_add_leaf_len(tree, "Saver model", desc->saver.sig,
		sizeof(desc->saver.sig), "SHIFT-JIS");
	if (desc->saver.data) {
		tree_add_leaf_len(tree, "Saver data", desc->saver.data,
			desc->saver.len, "SHIFT-JIS");
	}
	tree_bud_leaf(tree, "Depth",
		(struct wu_leaf){.val.u = desc->depth, .type = wu_leaf_unsigned});
}

static void cleanup(void *ptr) {
	pi_cleanup(ptr);
}
static size_t dec(const void *restrict ptr, struct raw_img *img) {
	return pi_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct raw_img *img) {
	return pi_read_header(ptr, img);
}
static enum wu_error open(void *restrict ptr, FILE *ifp) {
	return pi_open_file(ptr, ifp);
}

enum wu_error pi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pi_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, cleanup);
}
