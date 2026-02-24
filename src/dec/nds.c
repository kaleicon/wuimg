// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include "lib/nds.h"
#include "raster/fmt.h"
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
struct wu_state *state, const enum image_event ev) {
	(void)state;
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

static struct wu_st init_nclr(struct image_file *infile) {
	return nclr_init(infile->dec_state, infile->map);
}


static void end_ncgr(struct image_file *infile) {
	ncgr_cleanup(infile->dec_state);
}

static struct wu_st event_ncgr(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
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

static struct wu_st init_ncgr(struct image_file *infile) {
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
struct wu_state *state, const enum image_event ev) {
	(void)state;
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

static struct wu_st init_nscr(struct image_file *infile) {
	return nscr_init(infile->dec_state, infile->sub_img, infile->map,
		infile->name);
}


static struct wu_st event_ancl(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		nclr_img_info(infile->sub_img);
		return WU_OK;
	case ev_subcycle:
		return ancl_into_img(infile->sub_img, infile->ifp);
	default: break;
	}
	return WU_NO_CHANGE;
}


static struct wu_st event_atex(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		return atex_parse(infile->sub_img, infile->ifp, infile->name);
	case ev_subcycle:
		return fmt_load_raster_st(infile->sub_img, infile->ifp);
	default: break;
	}
	return WU_NO_CHANGE;
}


static struct wu_st event_bgd(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		return bgd_init(infile->dec_state, infile->sub_img, infile->map);
	case ev_subcycle:
		return bgd_decode(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}


static struct wu_st init_r00(struct image_file *infile) {
	size_t decoded = 0;
	struct wu_st st = WU_OK;
	for (;;) {
		if (!realloc_sub_images(infile, decoded + 1)) {
			st = WUERR_HERE(wu_alloc_error);
			break;
		}
		struct wuimg *img = infile->sub_img + decoded;
		st = r00_parse_next(img, infile->ifp);
		if (!wu_isok(st)) {
			break;
		}
		enum wu_error e = wuimg_alloc_limit(img, infile->conf);
		if (e != wu_ok) {
			st = WUERR_HERE(e);
			break;
		}
		st = fmt_load_raster_st(img, infile->ifp);
		if (!wu_isok(st)) {
			break;
		}
		++decoded;
	}
	return decoded
		? WUERR_CHECK(image_file_total_decoded(infile, decoded))
		: st;
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
const struct image_fn ancl_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.event = event_ancl,
};
const struct image_fn atex_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.event = event_atex,
};
const struct image_fn bgd_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct bgd_desc),
	.event = event_bgd,
};
const struct image_fn r00_fn = {
	.init = init_r00,
};
