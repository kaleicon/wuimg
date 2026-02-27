// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/qoi.h"

static struct wu_st init_qoi(struct image_file *infile) {
	struct wuptr data;
	struct wu_st st = qoi_parse(&data, infile->sub_img, infile->map);
	if (wu_isok(st)) {
		if (wuimg_exceeds_limit(infile->sub_img, infile->conf)) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else {
			st = qoi_decode(data, infile->sub_img);
		}
	}
	return st;
}

const struct image_fn qoi_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_qoi,
};
