// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/ant.h"
#include "wudefs.h"

static struct wu_st init_ant(struct image_file *infile,
const struct wu_conf *conf) {
	struct ant_desc desc;
	struct wu_st st = ant_init(&desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(infile->sub_img, conf);
		if (e == wu_ok) {
			st = ant_decode(&desc, infile->sub_img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}

const struct image_fn ant_fn = {
	.alloc_single = true,
	.init = init_ant,
};
