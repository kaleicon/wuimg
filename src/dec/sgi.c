// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/sgi.h"

static void metadata(const void *restrict ptr, struct wu_tree *tree) {
	const struct sgi_desc *desc = ptr;
	tree_add_leaf_limit(tree, "Image name", WUPTR_ARRAY(desc->name), NULL);
	tree_bud_leaf_bool(tree, "Compressed",
		desc->compression != sgi_uncompressed);
}

static size_t dec(const void *restrict ptr, struct wuimg *img) {
	return sgi_decode(ptr, img);
}
static enum wu_error parse(void *restrict ptr, struct wuimg *img) {
	return sgi_parse_header(ptr, img);
}
static enum wu_error open(void *restrict ptr, struct image_file *infile) {
	return sgi_open_file(ptr, infile->ifp);
}

enum wu_error sgi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct sgi_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, NULL);
}

const struct image_fn sgi_fn = {.dec = sgi_dec};
