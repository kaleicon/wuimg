// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include "wudefs.h"
#include "raster/fmt.h"
#include "lib/wbmp.h"

static struct wu_st event_wbmp(struct image_file *infile,
struct wu_state *state, enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		return wbmp_open_file(infile->sub_img, infile->ifp);
	case ev_subcycle:
		return fmt_load_raster_st(infile->sub_img, infile->ifp);
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn wbmp_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.event = event_wbmp,
};
