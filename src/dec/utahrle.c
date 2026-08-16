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

static struct wu_st event_utah(struct image_file *infile, struct wu_state *state,
const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		get_utah_metadata(infile->dec_state, infile);
		return WU_OK;
	case ev_subcycle:
		return utah_decode(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_utah(struct image_file *infile) {
	return utah_parse(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn utahrle_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct utah_desc),
	.init = init_utah,
	.event = event_utah,
};
