#include "rast_utils.h"
#include "lib/mag.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct mag_desc *desc = ptr;
	tree_add_leaf_limit(tree, "Model", desc->model, sizeof(desc->model), NULL);
	tree_add_leaf_utf8(tree, "Code", mag_model_code_str(desc->code));
	if (desc->code == mag_model_msx) {
		tree_add_leaf_utf8(tree, "Screen mode",
			mag_msx_screen_str(desc->msx.screen));
		tree_bud_leaf(tree, "Interlace", (struct wu_leaf)
			{.type = wu_leaf_bool, .val.b = desc->msx.interlace});
	}
	tree_add_leaf_limit(tree, "Comment", desc->comment.data,
		desc->comment.area_len, NULL);
}

static void cleanup(void *restrict desc) {
	mag_cleanup(desc);
}
static size_t dec(const void *restrict desc, struct raw_img *img) {
	return mag_decode(desc, img);
}
static enum wu_error parse(void *restrict desc, struct raw_img *img) {
	return mag_parse(desc, img);
}
static enum wu_error open(void *restrict desc, FILE *ifp) {
	return mag_open(desc, ifp);
}

enum wu_error mag_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct mag_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc,
		open, parse, metadata, dec, cleanup);
}
