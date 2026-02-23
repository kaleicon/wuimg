// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/utahrle.h"
#include "misc/math.h"
#include "wudefs.h"

static void get_utah_metadata(const struct utah_desc *desc,
struct image_file *infile) {
	struct wuimg *img = infile->sub_img;
	if (desc->pal_ch) {
		tree_bud_leaf_u(&infile->metadata, "Palette channels",
			desc->pal_ch);
	}
	if (desc->bg) {
		const size_t lim = sizeof(infile->bg) - 1;
		const size_t len = zumin(desc->channels, lim);
		const bool inc = desc->channels != 1;
		uint8_t *dst = (uint8_t *)&infile->bg;
		for (unsigned i = 0; i < len; ++i) {
			dst[i] = desc->bg[i*inc];
		}
		dst[lim] = 0xff;
	}
	if (desc->comm.len) {
		bool more;
		size_t pos = 0;
		struct wuptr k, v;
		while ( (more = utah_next_comment(desc, img, &pos, &k, &v)) ) {
			// TODO: sanitize key so it can be used directly
			const struct wuptr c = {
				.ptr = k.ptr,
				.len = (size_t)(v.ptr + v.len - k.ptr),
			};
			tree_add_leaf_len(&infile->metadata, "Comment", c, NULL);
		}
	}
}

static struct wu_st init_utah(struct image_file *infile) {
	struct utah_desc desc;
	struct wuimg *img = infile->sub_img;
	struct wu_st st = utah_parse(&desc, img, infile->map);
	if (wu_isok(st)) {
		if (wuimg_exceeds_limit(img, infile->conf)) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else {
			get_utah_metadata(&desc, infile);
			st = utah_decode(&desc, img);
		}
	}
	return st;
}

const struct image_fn utahrle_fn = {
	.alloc_single = true,
	.mmap = true,
	.init = init_utah,
};
