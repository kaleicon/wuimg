// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/gpc.h"

static void end_gpc(struct image_file *infile) {
	gpc_cleanup(infile->dec_state);
}

static struct wu_st event_gpc(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c;
	struct gpc_desc *desc = infile->dec_state;
	const uint32_t i = (uint32_t)state->idx;
	struct wuimg *img = infile->sub_img + i;
	struct wu_st st = WU_NO_CHANGE;
	switch (ev) {
	case ev_metadata:
		st = gpc_set_image(desc, img, i);
		if (wu_isok(st)) {
			struct wutree *tree = wuimg_get_metadata(img);
			if (tree) {
				tree_bud_leaf_u(tree, "X", desc->cur.x);
				tree_bud_leaf_u(tree, "Y", desc->cur.y);
			}
		}
		break;
	case ev_subcycle:
		return gpc_decode(desc, img);
	default: break;
	}
	return st;
}

static struct wu_st init_gpc(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	struct gpc_desc *desc = infile->dec_state;
	struct wu_st st = gpc_parse(desc, infile->map);
	if (wu_isok(st)) {
		infile->nr = desc->nb;
		tree_add_leaf_len(&infile->metadata, "Maker", desc->maker,
			"SHIFT-JIS");
	}
	return st;
}

static struct wu_st init_clm(struct image_file *infile,
const struct wu_conf *conf) {
	struct wuimg *img = infile->sub_img;
	struct wu_st st = clm_parse(infile->ifp, img);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(img, conf);
		if (e == wu_ok) {
			st = clm_load(infile->ifp, img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}

const struct image_fn gpc_fn = {
	.mmap = true,
	.state_size = sizeof(struct gpc_desc),
	.alloc_on_subcycle = true,
	.init = init_gpc,
	.event = event_gpc,
	.end = end_gpc,
};
const struct image_fn clm_fn = {
	.alloc_single = true,
	.init = init_clm,
};
