// SPDX-License-Identifier: 0BSD
#include "wudefs.h"
#include "lib/q4.h"

static enum wu_error q4_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct wuimg *img = infile->sub_img;
	enum wu_error st = q4_info(img);
	if (st == wu_ok) {
		if (!wuimg_exceeds_limit(img, wuconf)) {
			struct q4_desc desc;
			st = q4_open(&desc, infile->map);
			if (st == wu_ok) {
				tree_bud_leaf_time(&infile->metadata,
					"Approximate date",
					q4_approximate_date(&desc));
				st = q4_decode(&desc, img)
					? wu_ok : wu_decoding_error;
			}
			return st;
		}
		return wu_exceeds_size_limit;
	}
	return st;
}

const struct image_fn q4_fn = {
	.mmap = true,
	.alloc_single = true,
	.dec = q4_dec,
};
