#include "rast_utils.h"
#include "lib/prt.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct prt_desc *desc = ptr;
	const struct wu_tree_sap sap[] = {
		{"Version", {wu_leaf_unsigned, {.u = desc->version}}},
		{"Depth", {wu_leaf_unsigned, {.u = desc->depth}}},
		{"Mask", {wu_leaf_bool, {.b = desc->mask}}},
		{"X", {wu_leaf_unsigned, {.u = desc->x}}},
		{"Y", {wu_leaf_unsigned, {.u = desc->y}}},
	};
	const size_t len = ARRAY_LEN(sap) - (desc->version == prt_v102 ? 0 : 2);
	tree_bud_leaves(tree, sap, len);
}

static void cleanup(void *ptr) {
	prt_cleanup(ptr);
}
static size_t dec(const void *restrict ptr, struct raw_img *img) {
	return prt_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct raw_img *img) {
	return prt_parse(ptr, img);
}
static enum wu_error open(void *restrict ptr, FILE *ifp) {
	return prt_open(ptr, ifp);
}

enum wu_error prt_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct prt_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, cleanup);
}
