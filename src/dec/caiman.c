// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include "wudefs.h"
#include "lib/caiman.h"
#include "raster/fmt.h"

static struct wu_st event_caiman(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct caiman_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img + state->idx;
	switch (ev) {
	case ev_metadata:
		return caiman_img_info(desc, img, (uint16_t)state->idx);
	case ev_subcycle:
		return fmt_load_raster_st(img, desc->ifp);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_caiman(struct image_file *infile) {
	struct caiman_desc *desc = infile->dec_state;
	struct wu_st st = caiman_init(desc, infile->ifp);
	infile->nr = desc->nr_images;
	return st;
}

const struct image_fn caiman_fn = {
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct caiman_desc),
	.init = init_caiman,
	.event = event_caiman,
};
