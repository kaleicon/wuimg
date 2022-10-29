#include "rast_utils.h"
#include "lib/tlg.h"

static void metadata(const void *ptr, struct wu_tree *tree) {
	const struct tlg_desc *desc = ptr;
	tree_add_leaf_utf8(tree, "Version", tlg_version_str(desc->version));
}

static size_t dec(const void *ptr, struct wuimg *img) {
	return tlg_decode(ptr, img);
}
static enum wu_error parse(void *ptr, struct wuimg *img) {
	return tlg_read_header(ptr, img);
}
static enum wu_error mopen(void *ptr, const struct mp_parser mp) {
	return tlg_open_mem(ptr, mp);
}

enum wu_error tlg_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct tlg_desc desc;
	return rast_trivial_map(infile, wuconf, &desc, mopen, parse, metadata,
		dec, NULL);
}
