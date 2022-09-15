#include "rast_utils.h"
#include "lib/sgi.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct sgi_desc *desc = ptr;
	tree_add_leaf_limit(tree, "Image name", desc->name, sizeof(desc->name),
		NULL);
	tree_bud_leaf(tree, "Compressed",
		(struct wu_leaf){.val.b = (desc->compression != sgi_uncompressed),
			.type = wu_leaf_bool});
}

static size_t dec(const void *restrict ptr, struct raw_img *img) {
	return sgi_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct raw_img *img) {
	return sgi_parse_header(ptr, img);
}
static enum wu_error open(void *restrict ptr, FILE *ifp) {
	return sgi_open_file(ptr, ifp);
}

enum wu_error sgi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct sgi_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, NULL);
}
