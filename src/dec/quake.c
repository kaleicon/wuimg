// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "lib/quake.h"
#include "rast_utils.h"
#include "wudefs.h"

static void end_idsp(struct image_file *infile) {
	idsp_cleanup(infile->dec_state);
}

static struct wu_st event_idsp(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	if (ev != ev_subcycle) {
		return WU_NO_CHANGE;
	}

	struct idsp_desc *desc = infile->dec_state;
	while ((int)desc->cur_group <= state->idx) {
		struct wuimg *img = infile->sub_img + desc->cur_group;
		struct wu_st st = idsp_next_image(desc, img);
		if (!wu_isok(st)) {
			return st;
		}
		const enum wu_error e = wuimg_alloc_limit(img, conf);
		if (e != wu_ok) {
			return WUERR_HERE(e);
		}
		st = idsp_read_image(desc, img);
		if (!wu_isok(st)) {
			break;
		}
	}
	return WU_OK;
}

static struct wu_st init_idsp(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	struct idsp_desc *desc = infile->dec_state;
	struct wu_st st = idsp_init(desc, infile->ifp);
	if (!wu_isok(st)) {
		return st;
	}

	if (!alloc_sub_images(infile, desc->groups)) {
		return WUERR_HERE(wu_alloc_error);
	}

	struct wutree *tree = &infile->metadata;
	tree_bud_leaf_u(tree, "Version", desc->version);
	tree_add_leaf_utf8(tree, "Type", idsp_type_str(desc->type));
	if (desc->version == idsp_half_life) {
		tree_add_leaf_utf8(tree, "Alpha", idsp_alpha_str(desc->alpha));
	}
	tree_bud_leaf_u(tree, "Max width", desc->w);
	tree_bud_leaf_u(tree, "Max height", desc->h);
	tree_bud_leaf_f(tree, "Bounding radius", desc->radius);
	tree_bud_leaf_f(tree, "Beam length", desc->beam_length);
	tree_add_leaf_utf8(tree, "Synch", idsp_synch_str(desc->synch));
	return st;
}


static enum wu_error lmp_dec(struct image_file *infile,
const struct wu_conf *conf) {
	return rast_trivial_fread(infile, conf, lmp_init);
}

const struct image_fn idsp_fn = {
	.state_size = sizeof(struct idsp_desc),
	.init = init_idsp,
	.event = event_idsp,
	.end = end_idsp,
};
const struct image_fn lmp_fn = {
	.alloc_single = true,
	.dec = lmp_dec,
};
