// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/q4.h"

static struct wu_st event_q4(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event ev) {
	(void)wuconf; (void)state;
	struct q4_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_time(&infile->metadata, "Approximate date?",
			q4_approximate_date(desc));
		return q4_img_info(infile->sub_img);
	case ev_subcycle:
		return q4_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_q4(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	infile->nr = 1;
	return q4_open(infile->dec_state, infile->map);
}

const struct image_fn q4_fn = {
	.mmap = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct q4_desc),
	.init = init_q4,
	.event = event_q4,
};
