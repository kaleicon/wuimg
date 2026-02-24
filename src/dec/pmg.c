// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/pmg.h"
#include "wudefs.h"

static struct wu_st event_pmg(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		return pmg_init(infile->map, infile->sub_img);
	case ev_subcycle:
		return pmg_decode(infile->map, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn pmg_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.event = event_pmg,
};
