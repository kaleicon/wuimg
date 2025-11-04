// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/gp4.h"

static struct wu_st init_gp4(struct image_file *infile,
const struct wu_conf *conf) {
	struct gp4_desc desc;
	struct wuimg *img = infile->sub_img;
	struct wu_st st = gp4_parse(&desc, infile->map, img);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(img, conf);
		if (e == wu_ok) {
			tree_bud_leaf_u(&infile->metadata, "X", desc.x);
			tree_bud_leaf_u(&infile->metadata, "Y", desc.y);
			st = gp4_decode(&desc, img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}

const struct image_fn gp4_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_gp4
};
