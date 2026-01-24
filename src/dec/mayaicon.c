// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/mayaicon.h"
#include "wudefs.h"

/* TODO: would be nice to somehow combine this and mgxicn.c, as they are
 * literal copies */

static struct wu_st event_mayaicon(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state,
const enum image_event ev) {
	struct wu_st st = WU_NO_CHANGE;
	if (ev == ev_subcycle) {
		struct mayaicon_desc *desc = infile->dec_state;
		const uint32_t i = (uint32_t)state->idx;
		while (desc->idx <= i) {
			struct wuimg *img = infile->sub_img + desc->idx;
			st = mayaicon_set_next(desc, img);
			if (!wu_isok(st)) {
				break;
			}
			enum wu_error err = wuimg_alloc_limit(img, conf);
			if (err != wu_ok) {
				st = WUERR_HERE(err);
				break;
			}
			st = mayaicon_load(desc, img);
			if (!wu_isok(st)) {
				break;
			}
		}
	}
	return st;
}

static struct wu_st init_mayaicon(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	struct mayaicon_desc *desc = infile->dec_state;
	struct wu_st st = mayaicon_init(desc, infile->ifp);
	infile->nr = desc->nr;
	return st;
}

const struct image_fn mayaicon_fn = {
	.state_size = sizeof(struct mayaicon_desc),
	.init = init_mayaicon,
	.event = event_mayaicon,
};
