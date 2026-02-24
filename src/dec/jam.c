// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/jam.h"

static struct wu_st event_jam(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct mparser *mp = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		*mp = mp_wuptr(infile->map);
		return jam_parse(mp, infile->sub_img);
	case ev_subcycle:
		return jam_decode(*mp, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn jam_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct mparser),
	.mmap = true,
	.event = event_jam,
};
