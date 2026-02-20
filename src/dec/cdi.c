// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <string.h>

#include "lib/cdi.h"
#include "wudefs.h"

static void end_cdi(struct image_file *infile) {
	cdi_cleanup(infile->dec_state);
}

static struct wu_st event_cdi(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf; (void)state;
	struct cdi_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		;struct wutree *meta = &infile->metadata;
		tree_add_leaf_utf8(meta, "Model", cdi_model_str(desc->model));
		if (desc->model == cdi_dyuv) {
			tree_add_leaf_utf8(meta, "DYUV start",
				cdi_dyuv_start_str(desc->dyuv_start));
		}
		if (desc->has_user_data) {
			tree_bud_leaf_u(meta, "USER size", desc->user_len);
		}
		struct cdi_ipar *ipar = &desc->ipar;
		if (ipar->off.x && ipar->off.y) {
			struct wutree *off = tree_add_branch(meta, "Offset");
			if (off) {
				tree_bud_leaf_d(off, "X", ipar->off.x);
				tree_bud_leaf_d(off, "Y", ipar->off.y);
			}
		}
		if (ipar->src.x && ipar->src.y) {
			struct wutree *src = tree_add_branch(meta, "Source page");
			if (src) {
				tree_bud_leaf_u(src, "Width", ipar->src.x);
				tree_bud_leaf_u(src, "Height", ipar->src.y);
			}
		}
		if (ipar->hotspot.x && ipar->hotspot.y) {
			struct wutree *hot = tree_add_branch(meta, "Hotspot");
			if (hot) {
				tree_bud_leaf_u(hot, "X", ipar->hotspot.x);
				tree_bud_leaf_u(hot, "Y", ipar->hotspot.y);
			}
		}
		memcpy(&infile->bg, &ipar->trans, sizeof(ipar->trans));
		return WU_OK;
	case ev_subcycle:
		return cdi_load(desc);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_cdi(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	return cdi_init(infile->dec_state, infile->sub_img, infile->ifp);
}

const struct image_fn cdi_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct cdi_desc),
	.init = init_cdi,
	.event = event_cdi,
	.end = end_cdi,
};
