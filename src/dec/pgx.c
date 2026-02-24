// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include "wudefs.h"
#include "lib/pgx.h"

static struct wu_st event_pgx(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct wuptr *comp = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		return pgx_read_header(comp, infile->map, infile->sub_img);
	case ev_subcycle:
		return pgx_decode(*comp, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn pgx_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct wuptr),
	.event = event_pgx,
};
