// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/spooky.h"

static struct wu_st init_tre(struct image_file *infile,
const struct wu_conf *conf) {
	struct tre_desc desc;
	struct wu_st st = tre_parse(&desc, infile->sub_img, infile->map);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(infile->sub_img, conf);
		if (e == wu_ok) {
			st = tre_decode(&desc, infile->sub_img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}


static struct wu_st event_trs(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf;
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

static struct wu_st init_trs(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	struct trs_desc *desc = infile->dec_state;
	struct wu_st st = trs_parse(desc, infile->map);
	if (wu_isok(st) && !alloc_sub_images(infile, desc->nr)) {
		st = WUERR_HERE(wu_alloc_error);
	}
	return st;
}

const struct image_fn tre_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_tre,
};
const struct image_fn trs_fn = {
	.mmap = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct trs_desc),
	.init = init_trs,
	.event = event_trs,
};
