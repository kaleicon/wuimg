// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include "lib/nds.h"
#include "wudefs.h"

static void get_texfmt_metadata(struct wutree *meta, const enum nds_texfmt fmt) {
	tree_add_leaf_utf8(meta, "Tex format", nds_texfmt_str(fmt));
}

static void get_nclr_metadata(struct wutree *meta, const struct nclr_desc *desc) {
	get_texfmt_metadata(meta, desc->fmt);
}

static void get_ncgr_metadata(struct wutree *meta, const struct ncgr_desc *desc) {
	get_texfmt_metadata(meta, desc->fmt);
	tree_add_leaf_utf8(meta, "Mapping", nds_mapping_str(desc->mapping));
	tree_add_leaf_utf8(meta, "Char format", nds_charfmt_str(desc->charfmt));
	tree_bud_leaf_u(meta, "Y Tiles", desc->h);
	tree_bud_leaf_u(meta, "X Tiles", desc->w);
	if (desc->pal) {
		struct wutree *sub = tree_add_branch(meta, "NCLR");
		if (sub) {
			get_nclr_metadata(sub, &desc->nclr);
		}
	}
}

static struct wu_st event_nclr(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf; (void)state;
	struct nclr_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		get_nclr_metadata(&infile->metadata, desc);
		return nclr_img_info(infile->sub_img);
	case ev_subcycle:
		return nclr_into_img(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_nclr(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	return nclr_init(infile->dec_state, infile->map);
}


static void end_ncgr(struct image_file *infile) {
	ncgr_cleanup(infile->dec_state);
}

static struct wu_st event_ncgr(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf; (void)state;
	struct ncgr_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		get_ncgr_metadata(&infile->metadata, desc);
		return ncgr_img_info(desc, infile->sub_img);
	case ev_subcycle:
		return ncgr_load(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_ncgr(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	struct wu_st st = ncgr_init(infile->dec_state, infile->map);
	if (wu_isok(st)) {
		struct wu_st st2 = ncgr_search_nclr(infile->dec_state,
			infile->name);
		if (!wu_isok(st2)) {
			image_file_strerror_append(infile, st2.msg);
		}
	}
	return st;
}


static void end_nscr(struct image_file *infile) {
	nscr_cleanup(infile->dec_state);
}

static struct wu_st event_nscr(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	(void)conf; (void)state;
	struct nscr_desc *desc = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		tree_add_leaf_utf8(&infile->metadata, "Color mode",
			nds_colormode_str(desc->color));
		tree_add_leaf_utf8(&infile->metadata, "Screen format",
			nds_scrfmt_str(desc->fmt));
		struct wutree *sub = tree_add_branch(&infile->metadata, "NCGR");
		if (sub) {
			get_ncgr_metadata(sub, &desc->ncgr);
		}
		return WU_OK;
	case ev_subcycle:
		return nscr_decode(desc, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_nscr(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	return nscr_init(infile->dec_state, infile->sub_img, infile->map,
		infile->name);
}

const struct image_fn nclr_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct nclr_desc),
	.init = init_nclr,
	.event = event_nclr,
};
const struct image_fn ncgr_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct ncgr_desc),
	.init = init_ncgr,
	.event = event_ncgr,
	.end = end_ncgr,
};
const struct image_fn nscr_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct nscr_desc),
	.init = init_nscr,
	.event = event_nscr,
	.end = end_nscr,
};
