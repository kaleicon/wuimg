// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/siff.h"
#include "wudefs.h"

static struct wu_st event_pim(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	return (ev == ev_subcycle)
		? pim_decode(infile->dec_state, infile->sub_img)
		: WU_NO_CHANGE;
}

static struct wu_st init_pim(struct image_file *infile) {
	return pim_parse(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn pim_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct pim_desc),
	.init = init_pim,
	.event = event_pim,
};
