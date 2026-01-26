// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include "wudefs.h"
#include "raster/fmt.h"
#include "lib/wbmp.h"

static struct wu_st event_wbmp(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event ev) {
	(void)wuconf; (void)state;
	return (ev == ev_subcycle)
		? fmt_load_raster_st(infile->sub_img, infile->ifp)
		: WU_NO_CHANGE;
}

static struct wu_st init_wbmp(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	return wbmp_open_file(infile->sub_img, infile->ifp);
}

const struct image_fn wbmp_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.init = init_wbmp,
	.event = event_wbmp,
};
