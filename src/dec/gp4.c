// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/gp4.h"

static enum wu_error gp4_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct gp4_desc desc;
	struct wuimg *img = infile->sub_img;
	enum wu_error st = gp4_parse(&desc, infile->map, img);
	if (st == wu_ok) {
		tree_bud_leaf_u(&infile->metadata, "X", desc.x);
		tree_bud_leaf_u(&infile->metadata, "Y", desc.y);
		if (!wuimg_exceeds_limit(img, conf)) {
			return gp4_decode(&desc, img)
				? wu_ok : wu_decoding_error;
		}
		return wu_exceeds_size_limit;
	}
	return st;
}

const struct image_fn gp4_fn = {
	.mmap = true,
	.alloc_single = true,
	.dec = gp4_dec,
};
