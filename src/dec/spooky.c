// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/spooky.h"

static struct wu_st event_tre(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		return tre_parse(infile->dec_state, infile->sub_img,
			infile->map);
	case ev_subcycle:
		return tre_decode(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}


static struct wu_st event_trs(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	const uint16_t i = (uint16_t)state->idx;
	struct wuimg *img = infile->sub_img + i;
	switch (ev) {
	case ev_metadata:
		return trs_set_image(infile->dec_state, img, i);
	case ev_subcycle:
		return trs_get_image(infile->dec_state, img, i);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_trs(struct image_file *infile) {
	struct trs_desc *desc = infile->dec_state;
	struct wu_st st = trs_parse(desc, infile->map);
	infile->nr = desc->nr;
	return st;
}

const struct image_fn tre_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct tre_desc),
	.event = event_tre,
};
const struct image_fn trs_fn = {
	.mmap = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct trs_desc),
	.init = init_trs,
	.event = event_trs,
};
