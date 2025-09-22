// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/hg3.h"

static struct wu_st init_hg3(struct image_file *infile,
const struct wu_conf *conf) {
	struct hg3_desc desc;
	struct wu_st st = hg3_open(&desc, infile->map);
	if (!wu_isok(st)) {
		return st;
	}

	size_t i = 0;
	while ( wu_isok(st = hg3_next_image(&desc)) ) {
		struct wuimg *img = infile->sub_img;
		if (i >= infile->nr) {
			img = realloc_sub_images(infile, i + 1);
			if (!img) {
				break;
			}
		}
		img += i;

		st = hg3_parse_image(&desc, img);
		if (wu_isok(st)) {
			enum wu_error e = wuimg_alloc_limit(img, conf);
			if (e == wu_ok) {
				st = hg3_decode(&desc, img);
				if (wu_isok(st)) {
					++i;
				} else {
					wuimg_clear(img);
				}
			}
		}
	}
	return i ? WUERR_CHECK(image_file_total_decoded(infile, i)) : st;
}

const struct image_fn hg3_fn = {
	.mmap = true,
	.init = init_hg3,
};
