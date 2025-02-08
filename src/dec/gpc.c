// SPDX-License-Identifier: 0BSD
#include "wudefs.h"
#include "lib/gpc.h"

static void end(struct image_file *infile) {
	gpc_cleanup(infile->dec_state);
}

static void get_metadata(const struct gpc_desc *desc, struct wuimg *img) {
	struct wutree *tree = wuimg_get_metadata(img);
	if (tree) {
		tree_bud_leaf_u(tree, "X", desc->cur.x);
		tree_bud_leaf_u(tree, "Y", desc->cur.y);
	}
}

static enum wu_error callback(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	enum wu_error st = wu_no_change;
	if (ev == ev_subcycle) {
		struct gpc_desc *desc = infile->dec_state;
		const uint32_t i = (uint32_t)state->idx;
		struct wuimg *img = infile->sub_img + i;
		st = gpc_set_image(desc, img, i);
		if (st == wu_ok) {
			if (!wuimg_exceeds_limit(img, conf)) {
				get_metadata(desc, img);
				return gpc_decode(desc, img)
					? wu_ok : wu_decoding_error;
			}
			return wu_exceeds_size_limit;
		}
	}
	return st;
}

static enum wu_error gpc_dec(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	struct gpc_desc *desc = infile->dec_state;
	enum wu_error st = gpc_init(desc, infile->map);
	if (st != wu_ok) {
		return st;
	}

	st = gpc_parse(desc);
	if (st != wu_ok) {
		return st;
	}

	if (desc->maker.len) {
		tree_add_leaf_len(&infile->metadata, "Maker", desc->maker,
			"SHIFT-JIS");
	}
	return alloc_sub_images(infile, desc->nb) ? wu_ok : wu_alloc_error;
}

static enum wu_error clm_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct wuimg *img = infile->sub_img;
	enum wu_error st = clm_parse(infile->ifp, img);
	if (st == wu_ok) {
		if (!wuimg_exceeds_limit(img, conf)) {
			return clm_load(infile->ifp, img)
				? wu_ok : wu_decoding_error;
		}
		return wu_exceeds_size_limit;
	}
	return st;
}

const struct image_fn gpc_fn = {
	.mmap = true,
	.state_size = sizeof(struct gpc_desc),
	.dec = gpc_dec,
	.callback = callback,
	.end = end,
};
const struct image_fn clm_fn = {
	.alloc_single = true,
	.dec = clm_dec,
};
