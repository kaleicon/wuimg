// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/atari.h"

static void atari_res_metadata(struct wutree *meta, const enum atari_st_res res) {
	tree_add_leaf_utf8(meta, "Resolution", atari_st_res_str(res));
}

/* Calamus Raster Graphic */
static struct wu_st init_crg(struct image_file *infile) {
	struct wu_st st = crg_get_info(infile->map, infile->sub_img);
	if (wu_isok(st)) {
		st = WUERR_CHECK(wuimg_alloc_limit(infile->sub_img, infile->conf));
		if (wu_isok(st)) {
			st = crg_decode(infile->map, infile->sub_img);
		}
	}
	return st;
}

/* Dali */
static struct wu_st init_dali(struct image_file *infile) {
	struct dali_desc desc;
	struct wuimg *img = infile->sub_img;
	struct wu_st st = dali_parse(&desc, img, infile->ifp, infile->ext);
	if (wu_isok(st)) {
		atari_res_metadata(&infile->metadata, desc.res);
		st = WUERR_CHECK(wuimg_alloc_limit(img, infile->conf));
		if (wu_isok(st)) {
			st = dali_decode(&desc, img);
		}
	}
	return st;
}

/* DEGAS */
static void end_degas(struct image_file *infile) {
	degas_cleanup(infile->dec_state);
}

static struct wu_st event_degas(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct degas_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	switch (ev) {
	case ev_metadata:
		;struct wutree *meta = &infile->metadata;
		atari_res_metadata(meta, desc->res);
		tree_bud_leaf_bool(meta, "Compressed", desc->compressed);
		tree_bud_leaf_bool(meta, "Elite", desc->is_elite);
		return WU_OK;
	case ev_subcycle:
		return degas_decode(desc, img);
	case ev_time:
		palette_cycle_render(img->u.palette, desc->cycle, state->time);
		return WU_OK;
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_degas(struct image_file *infile) {
	return degas_parse(infile->dec_state, infile->sub_img, infile->ifp);
}

/* EZ-Art Professional */
static struct wu_st event_ez(struct image_file *infile,
struct wu_state *_s, const enum image_event ev) {
	(void)_s;
	if (ev == ev_subcycle) {
		enum wu_error err = wuimg_alloc_limit(infile->sub_img,
			infile->conf);
		if (err == wu_ok) {
			struct mparser *mp = infile->dec_state;
			return ez_decode(*mp, infile->sub_img);
		}
		return WUERR_HERE(err);
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_ez(struct image_file *infile) {
	return ez_parse(infile->dec_state, infile->sub_img, infile->map);
}

/* GFA Raytrace */
static struct wu_st event_gfa(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	switch (ev) {
	case ev_subcycle:
		;enum wu_error err = wuimg_alloc_limit(infile->sub_img,
			infile->conf);
		if (err != wu_ok) {
			return WUERR_HERE(err);
		}
		// fallthrough
	case ev_frame:
		;uint8_t frame = (uint8_t)state->frame;
		return gfa_decode(infile->dec_state, infile->sub_img, frame);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_gfa(struct image_file *infile) {
	return gfa_init(infile->dec_state, infile->sub_img, infile->ifp);
}

/* MegaPaint */
static struct wu_st init_bld(struct image_file *infile) {
	struct bld_desc desc;
	struct wuimg *img = infile->sub_img;
	struct wu_st st = bld_parse(&desc, img, infile->ifp);
	if (wu_isok(st)) {
		st = WUERR_CHECK(wuimg_alloc_limit(img, infile->conf));
		if (wu_isok(st)) {
			tree_bud_leaf_bool(&infile->metadata, "Compressed", desc.compressed);
			st = bld_decode(&desc, img);
		}
	}
	return st;
}

/* Spectrum 512 */
static struct wu_st init_spu(struct image_file *infile) {
	struct spu_desc desc;
	struct wu_st st = spu_init(&desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		st = WUERR_CHECK(wuimg_alloc_limit(infile->sub_img, infile->conf));
		if (wu_isok(st)) {
			st = spu_decode(&desc, infile->sub_img);
		}
	}
	return st;
}

/* STAD PAC, Arabesque */
static struct wu_st init_stad(struct image_file *infile) {
	struct wuimg *img = infile->sub_img;
	struct stad_desc desc;
	struct wu_st st = stad_init(&desc, img, infile->map);
	if (wu_isok(st)) {
		st = WUERR_CHECK(wuimg_alloc_limit(img, infile->conf));
		if (wu_isok(st)) {
			tree_add_leaf_utf8_limit(&infile->metadata, "Variant",
				wuptr_mem(desc.sig, sizeof(desc.sig)));
			st = stad_decode(&desc, img);
		}
	}
	return st;
}

/* Tiny Stuff */
static void end_tiny(struct image_file *infile) {
	tiny_cleanup(infile->dec_state);
}

static struct wu_st event_tiny(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct tiny_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	switch (ev) {
	case ev_metadata:
		atari_res_metadata(&infile->metadata, desc->res);
		tree_bud_leaf_u(&infile->metadata, "Iterations", desc->iters);
		return WU_OK;
	case ev_subcycle:
		return tiny_decode(desc, img);
	case ev_time:
		palette_cycle_render(img->u.palette, desc->cycle, state->time);
		return WU_OK;
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_tiny(struct image_file *infile) {
	return tiny_parse(infile->dec_state, infile->sub_img, infile->map);
}


const struct image_fn crg_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_crg,
};
const struct image_fn dali_fn = {
	.alloc_single = true,
	.init = init_dali,
};
const struct image_fn degas_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct degas_desc),
	.init = init_degas,
	.event = event_degas,
	.end = end_degas,
};
const struct image_fn ez_fn = {
	.mmap = true,
	.alloc_single = true,
	.state_size = sizeof(struct mparser),
	.init = init_ez,
	.event = event_ez,
};
const struct image_fn gfa_fn = {
	.alloc_single = true,
	.state_size = sizeof(struct gfa_desc),
	.init = init_gfa,
	.event = event_gfa,
};
const struct image_fn bld_fn = {
	.alloc_single = true,
	.init = init_bld,
};
const struct image_fn spu_fn = {
	.alloc_single = true,
	.init = init_spu,
};
const struct image_fn stad_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_stad,
};
const struct image_fn tiny_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct tiny_desc),
	.init = init_tiny,
	.event = event_tiny,
	.end = end_tiny,
};
