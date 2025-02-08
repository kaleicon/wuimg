// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/pdt.h"

static void metadata(const void *ptr, struct wutree *tree) {
	const struct pdt_desc *desc = ptr;
	tree_add_leaf_utf8(tree, "Version", pdt_version_str(desc->version));
}

static void cleanup(struct image_file *infile) {
	pdt_cleanup(infile->dec_state);
}
static size_t dec(const void *ptr, struct wuimg *img) {
	return pdt_decode(ptr, img);
}
static enum wu_error parse(void *ptr, struct wuimg *img) {
	return pdt_parse_header(ptr, img);
}
static enum wu_error open(void *ptr, struct image_file *infile) {
	return pdt_open_mem(ptr, infile->map);
}

static enum wu_error pdt_dec(struct image_file *infile,
const struct wu_conf *conf) {
	return rast_trivial_dec(infile, conf, infile->dec_state, open, parse,
		metadata, dec);
}

const struct image_fn pdt_fn = {
	.mmap = true,
	.state_size = sizeof(struct pdt_desc),
	.dec = pdt_dec,
	.end = cleanup,
};
