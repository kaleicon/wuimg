// SPDX-License-Identifier: 0BSD
#include "wudefs.h"
#include "lib/hg3.h"

static enum wu_error hg3_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct hg3_desc desc;
	enum wu_error st = hg3_open(&desc, infile->map);
	if (st != wu_ok) {
		return st;
	}

	size_t i = 0;
	while ((st = hg3_next_image(&desc)) == wu_ok) {
		struct wuimg *img = infile->sub_img;
		if (i >= infile->nr) {
			img = realloc_sub_images(infile, i + 1);
			if (!img) {
				break;
			}
		}
		img += i;

		st = hg3_parse_image(&desc, img);
		if (st == wu_ok) {
			if (!wuimg_exceeds_limit(img, wuconf)) {
				if (hg3_decode(&desc, img)) {
					++i;
				} else {
					wuimg_clear(img);
				}
			}
		}
	}
	return i ? image_file_total_decoded(infile, i) : st;
}

const struct image_fn hg3_fn = {.mmap = true, .dec = hg3_dec};
