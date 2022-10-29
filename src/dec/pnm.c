#include <limits.h>

#include "wudefs.h"
#include "lib/pnm.h"

enum wu_error pnm_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct pnm_desc desc;
	enum wu_error st = pnm_open_file(&desc, infile->ifp, true);
	if (st != wu_ok) {
		return st;
	}

	st = pnm_parse_header(&desc);
	if (st) {
		return st;
	}

	if (wuimg_exceeds_limit(&desc.rast, wuconf)) {
		return wu_exceeds_size_limit;
	}

	tree_add_leaf_utf8(&infile->metadata, "Type", pnm_type_str(desc.type));

	if (!alloc_sub_images(infile, desc.nr)) {
		return wu_alloc_error;
	}

	size_t i = 0;
	while (i < infile->nr) {
		struct wuimg *img = infile->sub_img + i;
		if (!pnm_decode(&desc, img, i)) {
			break;
		}
		++i;
	}
	return image_file_total_decoded(infile, i);
}
