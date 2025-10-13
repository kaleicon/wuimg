// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/kyg.h"

static void get_kyg_meta(const struct kyg_desc *desc, struct wutree *metadata) {
	tree_add_leaf_len(metadata, "Comment", desc->comment, "SHIFT-JIS");
	tree_bud_leaf_u(metadata, "X", desc->x);
	tree_bud_leaf_u(metadata, "Y", desc->y);
}

static struct wu_st event_kyg(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf; (void)state;
	if (ev == ev_subcycle) {
		return kyg_decode(infile->dec_state, infile->sub_img);
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_kyg(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	struct wu_st st = kyg_parse(infile->dec_state, infile->sub_img,
		infile->map);
	if (wu_isok(st)) {
		get_kyg_meta(infile->dec_state, &infile->metadata);
	}
	return st;
}

const struct image_fn kyg_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct kyg_desc),
	.init = init_kyg,
	.event = event_kyg,
};
