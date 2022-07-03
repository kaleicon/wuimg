#include <string.h>

#include "wudefs.h"
#include "lib/sgi.h"

static void read_metadata(const struct sgi_desc *desc, struct wu_tree *tree) {
	if (strnlen(desc->name, sizeof(desc->name))) {
		tree_sprout_unsafe_leaf(tree, "Image name", desc->name,
			sizeof(desc->name));
	}
	tree_bud_leaf(tree, "Compressed",
		(struct wu_leaf){.val.b = (desc->compression != sgi_uncompressed),
			.type = wu_leaf_bool});
}

enum wu_error sgi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct sgi_desc desc;
	enum wu_error st = sgi_open_file(&desc, infile->ifp);
	if (st == wu_ok) {
		struct raw_img *img = alloc_sub_images(infile, 1);
		if (img) {
			st = sgi_parse_header(&desc, img);
			if (st == wu_ok) {
				read_metadata(&desc, &infile->metadata);
				if (raw_img_exceeds_limit(img, wuconf)) {
					return wu_exceeds_size_limit;
				}
				return sgi_decode(&desc, img)
					? wu_ok : wu_decoding_error;
			}
			return st;
		}
		return wu_alloc_error;
	}
	return st;
}
