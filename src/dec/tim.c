// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include "lib/tim.h"
#include "wudefs.h"

static void end_tim(struct image_file *infile) {
	tim_cleanup(infile->dec_state);
}

static struct wu_st event_tim(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c;
	if (ev == ev_subcycle) {
		const uint16_t idx = (uint16_t)state->idx;
		struct wuimg *img = infile->sub_img + idx;
		struct tim_desc *desc = infile->dec_state;
		if (idx) {
			tim_alt_clut(desc, infile->sub_img, img, idx);
			return WU_OK;
		}
		return tim_decode_main(desc, img);
	}
	return WU_NO_CHANGE;
}

static void get_metadata(const struct tim_desc *desc, struct wutree *tree) {
	struct wutree *offset = tree_add_branch(tree, "Offset");
	if (offset) {
		tree_bud_leaf_u(offset, "X", desc->x);
		tree_bud_leaf_u(offset, "Y", desc->y);
	}

	if (desc->clut.nb) {
		struct wutree *pal = tree_add_branch(tree, "CLUT");
		if (pal) {
			tree_bud_leaf_u(pal, "X", desc->clut.x);
			tree_bud_leaf_u(pal, "Y", desc->clut.y);
		}
	}
}

static struct wu_st init_tim(struct image_file *infile,
const struct wu_conf *conf) {
	struct tim_desc *desc = infile->dec_state;
	struct wu_st st = tim_parse(desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		if (wuimg_exceeds_limit(infile->sub_img, conf)) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else {
			get_metadata(desc, &infile->metadata);
			if (desc->clut.nb > 1
			&& !realloc_sub_images(infile, desc->clut.nb)) {
				st.msg = "couldn't allocate alternate palettes"
					", will only show first";
			}
		}
	}
	return st;
}

const struct image_fn tim_fn = {
	.state_size = sizeof(struct tim_desc),
	.alloc_single = true,
	.init = init_tim,
	.event = event_tim,
	.end = end_tim,
};
