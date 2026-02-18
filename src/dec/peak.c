// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include "lib/peak.h"
#include "wudefs.h"

static struct wu_st event_peak(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf; (void)state;
	struct peak_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_bud_leaf_u(&infile->metadata, "Sample depth",
			8u << desc->high_depth);
		return WU_OK;
	case ev_subcycle:
		return peak_graph(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_peak(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	return peak_init(infile->dec_state, infile->sub_img, infile->map);
}
static struct wu_st init_rpkn(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	return rpkn_init(infile->dec_state, infile->sub_img, infile->map);
}
static struct wu_st init_sfpk(struct image_file *infile,
const struct wu_conf *conf) {
	(void)conf;
	return sfpk_init(infile->dec_state, infile->sub_img, infile->map);
}

const struct image_fn peak_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct peak_desc),
	.init = init_peak,
	.event = event_peak,
};
const struct image_fn rpkn_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct peak_desc),
	.init = init_rpkn,
	.event = event_peak,
};
const struct image_fn sfpk_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct peak_desc),
	.init = init_sfpk,
	.event = event_peak,
};
