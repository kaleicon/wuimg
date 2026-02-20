// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "lib/g00.h"
#include "wudefs.h"

static void g00_end(struct image_file *infile) {
	g00_cleanup(infile->dec_state, infile->sub_img);
}

static struct wu_st event_g00(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event ev) {
	(void)state;
	struct g00_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_u(&infile->metadata, "Version", desc->version);
		return wuimg_exceeds_limit(img, wuconf)
			? WUERR_HERE(wu_exceeds_size_limit)
			: WU_OK;
	case ev_subcycle:
		return g00_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_g00(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	return g00_parse(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn g00_fn = {
	.mmap = true,
	.alloc_single = true,
	.state_size = sizeof(struct g00_desc),
	.init = init_g00,
	.event = event_g00,
	.end = g00_end,
};
