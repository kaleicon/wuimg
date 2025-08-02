// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/nokia.h"
#include "misc/math.h"
#include "wudefs.h"

static struct wu_st event_nlm(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c;
	struct wu_st st = wuerr(wu_no_change, NULL);
	if (ev == ev_subcycle) {
		const uint8_t idx = (uint8_t)state->idx;
		struct wuimg *img = infile->sub_img + idx;
		st = nlm_image_info(infile->dec_state, img);
		if (wu_isok(st)) {
			st = nlm_load(infile->dec_state, img, idx);
		}
	}
	return st;
}

static struct wu_st init_nlm(struct image_file *infile,
const struct wu_conf *conf) {
	struct nlm_desc *desc = infile->dec_state;
	struct wu_st st = nlm_parse(desc, infile->ifp);
	if (wu_isok(st)) {
		if (umax(desc->w, desc->h) > conf->max_img_size) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else {
			if (!alloc_sub_images(infile, desc->nr_images)) {
				st = WUERR_HERE(wu_alloc_error);
			} else {
				tree_add_leaf_utf8(&infile->metadata,
					"Logo type",
					nlm_logo_type_str(desc->logo_type));
			}
		}
	}
	return st;
}

static struct wu_st init_nol(struct image_file *infile,
const struct wu_conf *conf) {
	struct nol_desc desc;
	struct wu_st st = nol_parse(&desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		if (wuimg_exceeds_limit(infile->sub_img, conf)) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else {
			struct wutree *t = &infile->metadata;
			tree_add_leaf_utf8(t, "Type",
				desc.is_nol ? "NOL" : "NGG");
			tree_bud_leaf_u(t, "Mystery number", desc.mystery);
			if (desc.is_nol) {
				tree_bud_leaf_u(t, "Country code", desc.country);
				tree_bud_leaf_u(t, "Network code", desc.network);
			}
			st = nol_load(&desc, infile->sub_img);
		}
	}
	return st;
}

static struct wu_st init_npm(struct image_file *infile,
const struct wu_conf *conf) {
	struct npm_desc desc;
	struct wu_st st = npm_parse(&desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		if (wuimg_exceeds_limit(infile->sub_img, conf)) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else {
			tree_add_leaf_len(&infile->metadata, "Comment",
				npm_get_comment(&desc), NULL);
			tree_bud_leaf_u(&infile->metadata, "Mystery number",
				desc.mystery);
			st = npm_load(&desc, infile->sub_img);
		}
	}
	return st;
}

static void end_nsl(struct image_file *infile) {
	nsl_clean(infile->dec_state);
}

static struct wu_st init_nsl(struct image_file *infile,
const struct wu_conf *conf) {
	struct nsl_desc *desc = infile->dec_state;
	struct wu_st st = nsl_parse(desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		if (wuimg_exceeds_limit(infile->sub_img, conf)) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else {
			tree_add_leaf_len(&infile->metadata, "Version",
				wuptr_wustr(desc->vers), NULL);
			tree_add_leaf_len(&infile->metadata, "Model",
				wuptr_wustr(desc->modl), NULL);
			st = nsl_load(desc, infile->sub_img);
		}
	}
	return st;
}

const struct image_fn nlm_fn = {
	.state_size = sizeof(struct nlm_desc),
	.init = init_nlm,
	.event = event_nlm,
};
const struct image_fn nol_fn = {
	.alloc_single = true,
	.init = init_nol,
};
const struct image_fn npm_fn = {
	.alloc_single = true,
	.init = init_npm,
};
const struct image_fn nsl_fn = {
	.alloc_single = true,
	.state_size = sizeof(struct nsl_desc),
	.init = init_nsl,
	.end = end_nsl,
};
