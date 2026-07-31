// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/chunsoft.h"
#include "wudefs.h"

static void cleanup_sir0(struct image_file *infile) {
	sir0_spr_cleanup(infile->dec_state);
}

static struct wu_st event_sir0(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	const uint8_t i = (uint8_t)state->idx;
	struct wuimg *img = infile->sub_img + i;
	struct sir0_spr_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_subcycle:
		;const struct wu_st st = sir0_spr_assemble_info(desc, img, i);
		if (!wu_isok(st)) {
			return st;
		} else if (wuimg_exceeds_limit(img, infile->conf)) {
			return WUERR_HERE(wu_exceeds_size_limit);
		}
		return sir0_spr_assemble(desc, img);
	case ev_frame:
		return sir0_spr_assemble_frame(desc, img, i,
			(uint16_t)state->frame);
	default: break;
	}
	return wuerr(wu_no_change, NULL);
}

static struct wu_st init_sir0(struct image_file *infile) {
	struct sir0_spr_desc *desc = infile->dec_state;
	const struct wu_st st = sir0_spr_init(desc, infile->map);
	infile->nr = desc->nb_images;
	//tree_add_leaf_len(&infile->metadata, "Name", desc->name, NULL);
	return st;
}

static void cleanup_at6p(struct image_file *infile) {
	at6p_cleanup(infile->dec_state);
}

static struct wu_st init_at6p(struct image_file *infile) {
	struct at6p_desc *desc = infile->dec_state;
	struct wu_st st = at6p_unpack(desc, infile->map);
	if (wu_isok(st)) {
		struct wuimg *img = infile->sub_img;
		st = at6p_info(desc, img);
		if (wu_isok(st)) {
			st = wuimg_exceeds_limit(img, infile->conf)
				? WUERR_HERE(wu_exceeds_size_limit)
				: at6p_load(desc, img);
		}
	}
	return st;
}

const struct image_fn sir0_fn = {
	.mmap = true,
	.state_size = sizeof(struct sir0_spr_desc),
	.init = init_sir0,
	.event = event_sir0,
	.end = cleanup_sir0,
};
const struct image_fn at6p_fn = {
	.mmap = true,
	.state_size = sizeof(struct at6p_desc),
	.alloc_single = true,
	.init = init_at6p,
	.end = cleanup_at6p,
};
