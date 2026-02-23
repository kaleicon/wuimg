// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include "lib/piff.h"
#include "raster/fmt.h"
#include "wudefs.h"

static struct wu_st init_vvtp(struct image_file *infile) {
	struct wu_st st = vvtp_init(infile->ifp);
	if (!wu_isok(st)) {
		return st;
	}

	size_t i = 0;
	for (;;) {
		struct wuimg *img = realloc_sub_images(infile, infile->nr + 1);
		if (!img) {
			st = WUERR_HERE(wu_alloc_error);
			break;
		}
		img += i;
		st = vvtp_next(infile->ifp, img);
		if (!wu_isok(st)) {
			break;
		}

		enum wu_error e = wuimg_alloc_limit(img, infile->conf);
		if (e != wu_ok) {
			st = WUERR_HERE(e);
			break;
		}

		st = fmt_load_raster_st(img, infile->ifp);
		if (!wu_isok(st)) {
			break;
		}
		++i;
	}
	if (infile->nr > i) {
		realloc_sub_images(infile, i);
	}
	return i ? WU_OK : st;
}

const struct image_fn vvtp_fn = {
	.init = init_vvtp,
};
