// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/siff.h"
#include "wudefs.h"

static struct wu_st event_pim(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		return pim_parse(infile->dec_state, infile->sub_img,
			infile->map);
	case ev_subcycle:
		return pim_decode(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn pim_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct pim_desc),
	.event = event_pim,
};
