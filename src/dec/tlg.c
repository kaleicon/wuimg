#include "wudefs.h"
#include "rast_utils.h"
#include "lib/tlg.h"

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm) {
	struct tlg_desc desc;
	enum wu_error st = tlg_open_mem(&desc, mm);
	if (st != wu_ok) {
		return st;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	st = tlg_read_header(&desc, img);
	if (st != wu_ok) {
		return st;
	}

	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	tree_sprout_leaf(&infile->metadata, "Version",
		tlg_version_str(desc.version));
	return tlg_decode(&desc, img) ? wu_ok : wu_decoding_error;
}

enum wu_error tlg_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return rast_map_wrap(infile, wuconf, decode);
}
