// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/degas.h"

static void metadata(const void *restrict ptr, struct wu_tree *meta) {
	const struct degas_desc *desc = ptr;
	tree_add_leaf_utf8(meta, "Resolution", degas_res_str(desc->res));
}
static size_t dec(const void *restrict desc, struct wuimg *img) {
	return degas_decode(desc, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return degas_parse(desc, img);
}
static enum wu_error open(void *restrict desc, struct image_file *infile) {
	return degas_open(desc, infile->ifp);
}

static enum wu_error degas_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct degas_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, metadata,
		dec, NULL);
}

const struct image_fn degas_fn = {.mmap = false, .dec = degas_dec};
