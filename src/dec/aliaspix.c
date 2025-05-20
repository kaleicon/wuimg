// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/aliaspix.h"
#include "wudefs.h"

static struct wu_st init_aliaspix(struct image_file *infile,
const struct wu_conf *conf) {
	struct wuimg *img = infile->sub_img;
	struct aliaspix_desc desc;
	struct wu_st st = aliaspix_init(&desc, img, infile->map);
	if (wu_isok(st)) {
		tree_bud_leaf_u(&infile->metadata, "X", desc.x);
		tree_bud_leaf_u(&infile->metadata, "Y", desc.y);
		if (wuimg_exceeds_limit(img, conf)) {
			return WUERR_HERE(wu_exceeds_size_limit);
		}
		st = aliaspix_decode(&desc, img);
	}
	return st;
}

const struct image_fn aliaspix_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_aliaspix,
};
