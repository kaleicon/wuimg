#include "lib/prt.h"

void read_metadata(const struct prt_desc *desc, struct wu_tree *tree) {
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

enum wu_error prt_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct prt_desc desc;
	enum wu_error st = prt_open(&desc, infile->ifp);
	if (st == wu_ok) {
		struct raw_img *img = alloc_sub_images(infile, 1);
		if (img) {
			st = prt_parse(&desc, img);
			if (st == wu_ok) {
				if (!raw_img_exceeds_limit(img, wuconf)) {
					read_metadata(&desc, &infile->metadata);
					const size_t w = prt_decode(&desc, img);
					if (!w) {
						st = wu_decoding_error;
					}
				} else {
					st = wu_exceeds_size_limit;
				}
			}
			prt_cleanup(&desc);
		} else {
			return wu_alloc_error;
		}
	}
	return st;
}
