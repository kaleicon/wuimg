// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/pmg.h"
#include "wudefs.h"

static struct wu_st init_pmg(struct image_file *infile) {
	struct wu_st st = pmg_init(infile->map, infile->sub_img);
	if (wu_isok(st)) {
		st = WUERR_CHECK(wuimg_alloc_limit(infile->sub_img, infile->conf));
		if (wu_isok(st)) {
			st = pmg_decode(infile->map, infile->sub_img);
		}
	}
	return st;
}

const struct image_fn pmg_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_pmg,
};
