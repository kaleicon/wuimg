// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/pdt.h"

static void metadata(const void *ptr, struct wu_tree *tree) {
	const struct pdt_desc *desc = ptr;
	tree_add_leaf_utf8(tree, "Version", pdt_version_str(desc->version));
}

static size_t dec(const void *ptr, struct wuimg *img) {
	return pdt_decode(ptr, img);
}
static enum wu_error parse(void *ptr, struct wuimg *img) {
	return pdt_parse_header(ptr, img);
}
static enum wu_error open(void *ptr, struct image_file *infile) {
	return pdt_open_mem(ptr, &infile->map);
}

static enum wu_error pdt_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct pdt_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, NULL);
}

const struct image_fn pdt_fn = {.mmap = true, .dec = pdt_dec};
