#include "wudefs.h"
#include "rast_utils.h"
#include "lib/pdt.h"

static void metadata(const void *ptr, struct wu_tree *tree) {
	const struct pdt_desc *desc = ptr;
	tree_add_leaf_utf8(tree, "Version", pdt_version_str(desc->version));
}

static size_t dec(const void *ptr, struct raw_img *img) {
	return pdt_decode(ptr, img);
}
static enum wu_error parse(void *ptr, struct raw_img *img) {
	return pdt_parse_header(ptr, img);
}
static enum wu_error mopen(void *ptr, const struct mp_parser mp) {
	return pdt_open_mem(ptr, mp);
}

enum wu_error pdt_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pdt_desc desc;
	return rast_trivial_map(infile, wuconf, &desc, mopen, parse, metadata,
		dec, NULL);
}
