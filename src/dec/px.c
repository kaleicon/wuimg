// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "wudefs.h"
#include "lib/px.h"

static struct wu_st event_px(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c;
	const uint32_t idx = (uint32_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	switch (ev) {
	case ev_metadata: return px_set_info(infile->dec_state, img);
	case ev_subcycle: return px_get_image(infile->dec_state, img, idx);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_px(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	struct px_desc *desc = infile->dec_state;
	const struct wu_st st = px_parse(desc, infile->map);
	if (wu_isok(st)) {
		infile->nr = desc->nr;
	}
	return st;
}

const struct image_fn px_fn = {
	.mmap = true,
	.state_size = sizeof(struct px_desc),
	.alloc_on_subcycle = true,
	.init = init_px,
	.event = event_px,
};
