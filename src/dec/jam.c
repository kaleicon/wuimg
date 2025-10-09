// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/jam.h"

static struct wu_st init_jam(struct image_file *infile,
const struct wu_conf *conf) {
	struct mparser mp = mp_wuptr(infile->map);
	struct wu_st st = jam_parse(&mp, infile->sub_img);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(infile->sub_img, conf);
		if (e == wu_ok) {
			st = jam_decode(mp, infile->sub_img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}

const struct image_fn jam_fn = {
	.alloc_single = true,
	.mmap = true,
	.init = init_jam,
};
