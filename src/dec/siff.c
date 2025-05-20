// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/siff.h"
#include "wudefs.h"

static struct wu_st event_pim(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c; (void)state;
	struct wu_st st = wuerr(wu_no_change, NULL);
	if (ev == ev_subcycle) {
		st = pim_decode(infile->dec_state, infile->sub_img);
	}
	return st;
}

static struct wu_st init_pim(struct image_file *infile,
const struct wu_conf *conf) {
	struct pim_desc *desc = infile->dec_state;
	struct wu_st st = pim_parse(desc, infile->sub_img, infile->map);
	if (wu_isok(st) && wuimg_exceeds_limit(infile->sub_img, conf)) {
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
