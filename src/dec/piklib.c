// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include "wudefs.h"
#include "lib/piklib.h"

static struct wu_st event_piklib(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		return piklib_init(infile->dec_state, infile->sub_img,
			infile->ifp);
	case ev_subcycle:
		return piklib_load(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn piklib_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct piklib_desc),
	.event = event_piklib,
};
