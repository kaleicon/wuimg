// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/skyroads.h"

static struct wu_st event_skyroads(struct image_file *infile,
struct wu_state *_s, const enum image_event ev) {
	(void)_s;
	if (ev == ev_subcycle) {
		const struct mparser *mp = infile->dec_state;
		return skyroads_decode(*mp, infile->sub_img);
	}
	return wuerr(wu_no_change, NULL);
}

static struct wu_st init_skyroads(struct image_file *infile) {
	struct wu_st st = skyroads_parse(infile->dec_state, infile->sub_img,
		infile->map);
	if (wu_isok(st) && wuimg_exceeds_limit(infile->sub_img, infile->conf)) {
		st = WUERR_HERE(wu_exceeds_size_limit);
	}
	return st;
}

const struct image_fn skyroads_fn = {
	.mmap = true,
	.alloc_single = true,
	.state_size = sizeof(struct mparser),
	.init = init_skyroads,
	.event = event_skyroads,
};
