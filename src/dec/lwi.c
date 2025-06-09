// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/lwi.h"
#include "wudefs.h"

static struct wu_st init_lwi(struct image_file *infile,
const struct wu_conf *conf) {
	struct mparser mp = mp_wuptr(infile->map);
	struct wuimg *img = infile->sub_img;
	enum lwi_field type;
	struct wuptr data;
	struct wu_st st;
	while (wu_isok( (st = lwi_next_field(&mp, img, &type, &data)) )) {
		if (type == lwi_image) {
			st = wuimg_exceeds_limit(img, conf)
				? WUERR_HERE(wu_exceeds_size_limit)
				: lwi_decode(mp, img);
			break;
		} else {
			tree_add_leaf_len(&infile->metadata, lwi_field_str(type),
				data, NULL);
		}
	}
	return st;
}

const struct image_fn lwi_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_lwi,
};
