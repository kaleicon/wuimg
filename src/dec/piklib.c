// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include "wudefs.h"
#include "lib/piklib.h"

static struct wu_st event_piklib(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct wu_st st = WU_NO_CHANGE;
	if (ev == ev_subcycle) {
		st = piklib_load(infile->dec_state, infile->sub_img);
	}
	return st;
}

static struct wu_st init_piklib(struct image_file *infile) {
	return piklib_init(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn piklib_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct piklib_desc),
	.init = init_piklib,
	.event = event_piklib,
};
