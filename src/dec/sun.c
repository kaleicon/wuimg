#include "wudefs.h"
#include "lib/sun.h"

static void read_metadata(const struct sun_desc *desc, struct wu_tree *tree) {
	tree_bud_leaf(tree, "Compressed", (struct wu_leaf){
		.val.b = (desc->type == sun_byte_encoded), .type = wu_leaf_bool});
}

enum wu_error sun_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct sun_desc desc;
	enum wu_error st = sun_open_file(&desc, infile->ifp);
	if (st == wu_ok) {
		struct raw_img *img = alloc_sub_images(infile, 1);
		if (img) {
			st = sun_parse_header(&desc, img);
			if (st == wu_ok) {
				read_metadata(&desc, &infile->metadata);
				if (raw_img_exceeds_limit(img, wuconf)) {
					return wu_exceeds_size_limit;
				}
				return sun_decode(&desc, img)
					? wu_ok : wu_decoding_error;
			}
			return st;
		}
		return wu_alloc_error;
	}
	return st;
}
