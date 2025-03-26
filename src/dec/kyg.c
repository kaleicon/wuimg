// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/kyg.h"

static void meta(const void *restrict ptr, struct wutree *metadata) {
	const struct kyg_desc *desc = ptr;
	tree_add_leaf_len(metadata, "Comment", desc->comment, "SHIFT-JIS");
	tree_bud_leaf_u(metadata, "X", desc->x);
	tree_bud_leaf_u(metadata, "Y", desc->y);
}
static size_t dec(const void *restrict desc, struct wuimg *img) {
	return kyg_decode(desc, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return kyg_parse(desc, img);
}
static enum wu_error init(void *restrict desc, struct image_file *infile) {
	return kyg_identify(desc, infile->map);
}

static enum wu_error kyg_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct kyg_desc desc;
	return rast_trivial_dec(infile, conf, &desc, init, parse, meta, dec);
}

const struct image_fn kyg_fn = {
	.mmap = true,
	.dec = kyg_dec,
};
