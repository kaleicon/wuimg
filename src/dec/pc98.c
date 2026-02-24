// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/pc98.h"

static struct wu_st event_prs(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct prs_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_add_leaf_utf8(&infile->metadata, "Company",
			desc->micro_cabin ? "Micro Cabin" : "IDES");
		if (!desc->micro_cabin) {
			tree_bud_leaf_u(&infile->metadata, "X",
				desc->u.ides.x);
			tree_bud_leaf_u(&infile->metadata, "Y",
				desc->u.ides.y);
			tree_bud_leaf_u(&infile->metadata, "Planes",
				desc->u.ides.ch);
			tree_bud_leaf_u(&infile->metadata, "Transparent",
				desc->u.ides.trans);
		}
		return WU_OK;
	case ev_subcycle:
		return prs_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_prs(struct image_file *infile) {
	return prs_parse(infile->dec_state, infile->map, infile->sub_img);
}


static void end_gpc(struct image_file *infile) {
	gpc_cleanup(infile->dec_state);
}

static struct wu_st event_gpc(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
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

static struct wu_st init_gpc(struct image_file *infile) {
	struct gpc_desc *desc = infile->dec_state;
	struct wu_st st = gpc_parse(desc, infile->map);
	if (wu_isok(st)) {
		infile->nr = desc->nb;
		tree_add_leaf_len(&infile->metadata, "Maker", desc->maker,
			"SHIFT-JIS");
	}
	return st;
}

const struct image_fn prs_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct prs_desc),
	.init = init_prs,
	.event = event_prs,
};
const struct image_fn gpc_fn = {
	.mmap = true,
	.state_size = sizeof(struct gpc_desc),
	.alloc_on_subcycle = true,
	.init = init_gpc,
	.event = event_gpc,
	.end = end_gpc,
};
