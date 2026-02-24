// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/signum.h"

static struct wu_st event_imc(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		return imc_parse(infile->dec_state, infile->sub_img,
			infile->map);
	case ev_subcycle:
		return imc_decode(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn imc_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct imc_desc),
	.event = event_imc,
};
