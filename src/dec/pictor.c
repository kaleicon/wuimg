// SPDX-License-Identifier: 0BSD
#include "lib/pictor.h"
#include "misc/common.h"
#include "rast_utils.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct pictor_desc *desc = ptr;
	tree_bud_leaf_u(tree, "X", desc->x);
	tree_bud_leaf_u(tree, "Y", desc->y);
	tree_bud_leaf_u(tree, "Blocks", desc->blocks);
	tree_bud_leaf_u(tree, "Planes", desc->planes);
	tree_bud_leaf_u(tree, "Depth", desc->depth);

	const char *mode = pictor_video_mode(desc);
	if (mode) {
		tree_add_leaf_utf8(tree, "Video mode", mode);
	}

	const char *paltype = pictor_palette_str(desc->pal_type);
	if (paltype) {
		tree_add_leaf_utf8(tree, "Palette type", paltype);
	}
}

static size_t dec(const void *restrict ptr, struct wuimg *img) {
	return pictor_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct wuimg *img) {
	return pictor_read_header(ptr, img);
}
static enum wu_error open(void *restrict ptr, struct image_file *infile) {
	return pictor_open_file(ptr, infile->ifp);
}

enum wu_error pictor_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct pictor_desc desc;
	return rast_trivial_dec(infile, conf, &desc, open, parse, metadata,
		dec, NULL);
}

const struct image_fn pictor_fn = {.dec = pictor_dec};
