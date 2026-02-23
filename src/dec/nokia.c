// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/nokia.h"
#include "misc/math.h"
#include "wudefs.h"

static struct wu_st event_nlm(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	const uint8_t idx = (uint8_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	switch (ev) {
	case ev_metadata:
		return nlm_image_info(infile->dec_state, img);
	case ev_subcycle:
		return nlm_load(infile->dec_state, img, idx);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_nlm(struct image_file *infile) {
	struct nlm_desc *desc = infile->dec_state;
	struct wu_st st = nlm_parse(desc, infile->ifp);
	if (wu_isok(st)) {
		infile->nr = desc->nr_images;
		tree_add_leaf_utf8(&infile->metadata, "Logo type",
			nlm_logo_type_str(desc->logo_type));
	}
	return st;
}


static struct wu_st event_nol(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct nol_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		;struct wutree *t = &infile->metadata;
		tree_add_leaf_utf8(t, "Type",
			desc->is_nol ? "NOL" : "NGG");
		tree_bud_leaf_u(t, "Mystery number", desc->mystery);
		if (desc->is_nol) {
			tree_bud_leaf_u(t, "Country code", desc->country);
			tree_bud_leaf_u(t, "Network code", desc->network);
		}
		return WU_OK;
	case ev_subcycle:
		return nol_load(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_nol(struct image_file *infile) {
	return nol_parse(infile->dec_state, infile->sub_img, infile->ifp);
}


static struct wu_st event_npm(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct npm_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_add_leaf_len(&infile->metadata, "Comment",
			npm_get_comment(desc), NULL);
		tree_bud_leaf_u(&infile->metadata, "Mystery number",
			desc->mystery);
		return WU_OK;
	case ev_subcycle:
		return npm_load(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_npm(struct image_file *infile) {
	return npm_parse(infile->dec_state, infile->sub_img, infile->ifp);
}


static void end_nsl(struct image_file *infile) {
	nsl_clean(infile->dec_state);
}

static struct wu_st event_nsl(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct nsl_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_add_leaf_len(&infile->metadata, "Version",
			wuptr_wustr(desc->vers), NULL);
		tree_add_leaf_len(&infile->metadata, "Model",
			wuptr_wustr(desc->modl), NULL);
		return WU_OK;
	case ev_subcycle:
		return nsl_load(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_nsl(struct image_file *infile) {
	return nsl_parse(infile->dec_state, infile->sub_img, infile->ifp);
}


const struct image_fn nlm_fn = {
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct nlm_desc),
	.init = init_nlm,
	.event = event_nlm,
};
const struct image_fn nol_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct nol_desc),
	.init = init_nol,
	.event = event_nol,
};
const struct image_fn npm_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct npm_desc),
	.init = init_npm,
	.event = event_npm,
};
const struct image_fn nsl_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct nsl_desc),
	.init = init_nsl,
	.event = event_nsl,
	.end = end_nsl,
};
