// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/tim2.h"
#include "wudefs.h"

static struct wu_st event_tim2(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct wu_st st = WU_NO_CHANGE;
	if (ev == ev_subcycle) {
		const uint32_t idx = (uint32_t)state->idx;
		struct tim2_desc *desc = infile->dec_state;
		while (desc->idx <= idx) {
			struct wuimg *img = infile->sub_img + desc->idx;
			st = tim2_next(desc, img);
			if (!wu_isok(st)) {
				break;
			}
			enum wu_error e = wuimg_alloc_limit(img, infile->conf);
			if (e != wu_ok) {
				return WUERR_HERE(e);
			}
			struct wutree *m = wuimg_get_metadata(img);
			if (m) {
				tree_bud_leaf_u(m, "Mipmaps", desc->mipmaps);
				tree_bud_leaf_u(m, "TEXA TA0",
					desc->texa_fba_pabe & 0xff);
				tree_bud_leaf_u(m, "TEXA AEM",
					(desc->texa_fba_pabe >> 15) & 1);
				tree_bud_leaf_u(m, "TEXA TA1",
					(desc->texa_fba_pabe >> 16) & 0xff);
				if (desc->pal_depth) {
					tree_bud_leaf_u(m, "CLUT elems",
						desc->pal_elems);
					tree_bud_leaf_u(m, "CLUT depth",
						desc->pal_depth*8u + 8);
					tree_bud_leaf_bool(m, "CLUT compound",
						desc->pal_compound);
				}
			}
			st = tim2_load(desc, img);
			if (!wu_isok(st)) {
				break;
			}
		}
	}
	return st;
}

static struct wu_st init_tim2(struct image_file *infile) {
	struct tim2_desc *desc = infile->dec_state;
	struct wu_st st = tim2_init(desc, infile->ifp);
	if (wu_isok(st)) {
		infile->nr = desc->nr;
	}
	return st;
}

const struct image_fn tim2_fn = {
	.state_size = sizeof(struct tim2_desc),
	.init = init_tim2,
	.event = event_tim2
};
