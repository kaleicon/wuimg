// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/sgf.h"
#include "wudefs.h"

static struct wu_st event_sgf(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct wu_st st = WU_NO_CHANGE;
	if (ev == ev_subcycle) {
		struct sgf_desc *desc = infile->dec_state;
		while (desc->idx <= (size_t)state->idx) {
			struct wuimg *img = infile->sub_img + desc->idx;
			st = sgf_setup_next(desc, img);
			if (!wu_isok(st)) {
				break;
			}
			enum wu_error err = wuimg_alloc_limit(img, infile->conf);
			if (err != wu_ok) {
				st = WUERR_HERE(err);
				break;
			}
			st = sgf_decode(desc, img);
			if (!wu_isok(st)) {
				break;
			}
		}
	}
	return st;
}

static struct wu_st init_sgf(struct image_file *infile) {
	struct sgf_desc *desc = infile->dec_state;
	struct wu_st st = sgf_parse(desc, infile->map);
	infile->nr = desc->nr;
	return st;
}

const struct image_fn sgf_fn = {
	.mmap = true,
	.state_size = sizeof(struct sgf_desc),
	.init = init_sgf,
	.event = event_sgf,
};
