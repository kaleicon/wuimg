// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/skyroads.h"

static struct wu_st event_skyroads(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	if (ev == ev_subcycle) {
		const struct mparser *mp = infile->dec_state;
		return skyroads_decode(*mp, infile->sub_img);
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_skyroads(struct image_file *infile) {
	return skyroads_parse(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn skyroads_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct mparser),
	.init = init_skyroads,
	.event = event_skyroads,
};
