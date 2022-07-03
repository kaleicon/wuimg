#include "wudefs.h"
#include "rast_utils.h"
#include "lib/pdt.h"

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm) {
	struct pdt_desc desc;
	enum wu_error st = pdt_open_mem(&desc, mm);
	if (st != wu_ok) {
		return st;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	st = pdt_parse_header(&desc, img);
	if (st != wu_ok) {
		return st;
	}

	tree_bud_leaf(&infile->metadata, "Version",
		(struct wu_leaf){.val.u = desc.version - '0' + 10, .type = wu_leaf_unsigned});

	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	return pdt_decode(&desc, img) ? wu_ok : wu_decoding_error;
}

enum wu_error pdt_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return rast_map_wrap(infile, wuconf, decode);
}
