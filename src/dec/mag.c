// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "rast_utils.h"
#include "lib/mag.h"

static void metadata(const void *restrict ptr, struct wutree *tree) {
	const struct mag_desc *desc = ptr;
	tree_add_leaf_len(tree, "Model", WUPTR_ARRAY(desc->model), "SHIFT-JIS");
	tree_add_leaf_utf8(tree, "Code", mag_model_code_str(desc->code));
	if (desc->code == mag_model_msx) {
		tree_add_leaf_utf8(tree, "MSX Screen mode",
			mag_msx_screen_str(desc->msx.screen));
		tree_bud_leaf_bool(tree, "Interlace", desc->msx.interlace);
	}
	tree_add_leaf_utf8(tree, "Screen mode",
		mag_screen_mode_str(desc->screen_mode));
	tree_add_leaf_len(tree, "Comment", desc->comm, "SHIFT_JIS");
	tree_add_leaf_len(tree, "Dummy", desc->dummy, NULL);
}

static void cleanup(struct image_file *infile) {
	mag_cleanup(infile->dec_state);
}
static size_t dec(const void *restrict desc, struct wuimg *img) {
	return mag_decode(desc, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return mag_parse(desc, img);
}
static enum wu_error open(void *restrict desc, struct image_file *infile) {
	return mag_init(desc, infile->map);
}

static enum wu_error mag_dec(struct image_file *infile,
const struct wu_conf *conf) {
	return rast_trivial_dec(infile, conf, infile->dec_state,
		open, parse, metadata, dec);
}

const struct image_fn mag_fn = {
	.mmap = true,
	.state_size = sizeof(struct mag_desc),
	.dec = mag_dec,
	.end = cleanup,
};
