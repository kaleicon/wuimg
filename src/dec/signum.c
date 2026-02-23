// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/signum.h"

static struct wu_st event_imc(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	return (ev == ev_subcycle)
		? imc_decode(infile->dec_state, infile->sub_img)
		: WU_NO_CHANGE;
}

static struct wu_st init_imc(struct image_file *infile) {
	return imc_parse(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn imc_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct imc_desc),
	.init = init_imc,
	.event = event_imc,
};
