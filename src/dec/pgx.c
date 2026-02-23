// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include "wudefs.h"
#include "lib/pgx.h"

static struct wu_st init_pgx(struct image_file *infile) {
	struct wuptr comp;
	struct wu_st st = pgx_read_header(&comp, infile->map, infile->sub_img);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(infile->sub_img,
			infile->conf);
		if (e == wu_ok) {
			st = pgx_decode(comp, infile->sub_img);
		} else {
			st = WUERR_HERE(wu_exceeds_size_limit);
		}
	}
	return st;
}

const struct image_fn pgx_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_pgx,
};
