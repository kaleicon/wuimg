// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/ea.h"
#include "wudefs.h"

static struct wu_st init_eafnt(struct image_file *infile,
const struct wu_conf *conf) {
	struct eafnt_desc desc;
	struct wuimg *img = infile->sub_img;
	struct wu_st st = eafnt_init(&desc, img, infile->ifp);
	if (wu_isok(st)) {
		tree_bud_leaf_u(&infile->metadata, "Characters", desc.chars);
		tree_bud_leaf_u(&infile->metadata, "Image code", desc.image_code);
		enum wu_error err = wuimg_alloc_limit(img, conf);
		if (err == wu_ok) {
			st = eafnt_load(&desc, img);
		} else {
			st = WUERR_HERE(err);
		}
	}
	return st;
}

const struct image_fn eafnt_fn = {
	.alloc_single = true,
	.init = init_eafnt,
};
