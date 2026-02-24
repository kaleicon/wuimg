// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/ant.h"
#include "wudefs.h"

static struct wu_st event_ant(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct ant_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		return ant_init(desc, infile->sub_img, infile->ifp);
	case ev_subcycle:
		return ant_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn ant_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct ant_desc),
	.event = event_ant,
};
