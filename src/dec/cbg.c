// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "rast_utils.h"
#include "lib/cbg.h"

static void metadata(const void *restrict ptr, struct wutree *metadata) {
	const struct cbg_desc *desc = ptr;
	tree_bud_leaf_u(metadata, "Version", desc->version);
}
static size_t dec(const void *restrict desc, struct wuimg *img) {
	return cbg_decode(desc, img);
}
static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return cbg_parse(desc, img);
}
static enum wu_error init(void *restrict desc, struct image_file *infile) {
	return cbg_init(desc, infile->map);
}

static enum wu_error cbg_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct cbg_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, init, parse,
		metadata, dec);
}

const struct image_fn cbg_fn = {.mmap = true, .dec = cbg_dec};
