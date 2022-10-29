#include "lib/pictor.h"
#include "misc/common.h"
#include "rast_utils.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct pictor_desc *desc = ptr;
	const struct wu_tree_sap sap[] = {
		{"X", {wu_leaf_unsigned, {.u = desc->x}}},
		{"Y", {wu_leaf_unsigned, {.u = desc->y}}},
		{"Compressed blocks", {wu_leaf_unsigned, {.u = desc->blocks}}},
		{"Planes", {wu_leaf_unsigned, {.u = desc->planes}}},
		{"Depth", {wu_leaf_unsigned, {.u = desc->depth}}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));

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
static enum wu_error open(void *restrict ptr, FILE *ifp) {
	return pictor_open_file(ptr, ifp);
}

enum wu_error pictor_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct pictor_desc desc;
	return rast_trivial_dec(infile, conf, &desc, open, parse, metadata,
		dec, NULL);
}
