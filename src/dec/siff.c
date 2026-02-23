// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/siff.h"
#include "wudefs.h"

static struct wu_st event_pim(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	if (ev == ev_subcycle) {
		return pim_decode(infile->dec_state, infile->sub_img);
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_pim(struct image_file *infile) {
	struct pim_desc *desc = infile->dec_state;
	struct wu_st st = pim_parse(desc, infile->sub_img, infile->map);
	if (wu_isok(st) && wuimg_exceeds_limit(infile->sub_img, infile->conf)) {
		st = WUERR_HERE(wu_exceeds_size_limit);
	}
	return st;
}

const struct image_fn pim_fn = {
	.mmap = true,
	.alloc_single = true,
	.state_size = sizeof(struct pim_desc),
	.init = init_pim,
	.event = event_pim,
};
