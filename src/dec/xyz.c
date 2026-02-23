// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/xyz.h"

static struct wu_st init_xyz(struct image_file *infile) {
	struct wu_st st = xyz_parse(infile->sub_img, infile->map);
	if (wu_isok(st)) {
		if (wuimg_exceeds_limit(infile->sub_img, infile->conf)) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else {
			st = xyz_decode(infile->sub_img, infile->map);
		}
	}
	return st;
}

const struct image_fn xyz_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_xyz,
};
