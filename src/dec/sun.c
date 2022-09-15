#include "rast_utils.h"
#include "lib/sun.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct sun_desc *desc = ptr;
	tree_bud_leaf(tree, "Compressed", (struct wu_leaf){
		.val.b = (desc->type == sun_byte_encoded), .type = wu_leaf_bool});
}

static size_t dec(const void *restrict ptr, struct raw_img *img) {
	return sun_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct raw_img *img) {
	return sun_parse_header(ptr, img);
}
static enum wu_error open(void *restrict ptr, FILE *ifp) {
	return sun_open_file(ptr, ifp);
}

enum wu_error sun_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct sun_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, NULL);
}
