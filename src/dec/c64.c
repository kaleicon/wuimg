// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2023 kaleido
#include "rast_utils.h"
#include "lib/c64.h"

static void meta(const void *restrict ptr, struct wutree *tree) {
	const struct c64_desc *desc = ptr;
	tree_add_leaf_utf8(tree, "Type", c64_fmt_str(desc->fmt));
	tree_add_leaf_utf8(tree, "Mode", c64_mode_str(desc->info.mode));
}
static size_t dec(const void *restrict desc, struct wuimg *img) {
	return c64_decode(desc, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return c64_set(desc, img);
}
static enum wu_error init(void *restrict desc, struct image_file *infile) {
	return c64_guess(desc, infile->map, infile->ext);
}

static enum wu_error c64_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct c64_desc desc;
	return rast_trivial_dec(infile, conf, &desc, init, parse, meta, dec);
}

const struct image_fn c64_fn = {
	.mmap = true,
	.dec = c64_dec,
};
