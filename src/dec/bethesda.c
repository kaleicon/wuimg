// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "lib/bethesda.h"
#include "wudefs.h"

static void end_fnhd(struct image_file *infile) {
	fnhd_cleanup(infile->dec_state);
}

static struct wu_st event_fnhd(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	struct wu_st st = WU_NO_CHANGE;
	if (ev == ev_subcycle) {
		struct fnhd_desc *desc = infile->dec_state;
		while (desc->cur <= (uint16_t)state->idx) {
			struct wuimg *img = infile->sub_img + desc->cur;
			st = fnhd_next_glyph(desc, img);
			if (!wu_isok(st)) {
				break;
			}
			enum wu_error e = wuimg_alloc_limit(img, conf);
			if (e != wu_ok) {
				st = WUERR_HERE(e);
				break;
			}
			st = fnhd_load_glyph(desc, img);
			if (!wu_isok(st)) {
				break;
			}
		}
	}
	return st;
}

static struct wu_st init_fnhd(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	struct fnhd_desc *desc = infile->dec_state;
	struct wu_st st = fnhd_init(desc, infile->ifp);
	if (wu_isok(st)) {
		infile->nr = desc->glyphs;
		const struct wuptr comm = wuptr_mem(desc->desc,
			strnlen((char *)desc->desc, sizeof(desc->desc)));
		tree_add_leaf_len(&infile->metadata, "Comment", comm, NULL);
	}
	return st;
}


static void end_gxa(struct image_file *infile) {
	gxa_cleanup(infile->dec_state);
}

static struct wu_st event_gxa(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	struct wu_st st = WU_NO_CHANGE;
	if (ev == ev_subcycle) {
		struct gxa_desc *desc = infile->dec_state;
		while (desc->cur <= (uint16_t)state->idx) {
			struct wuimg *img = infile->sub_img + desc->cur;
			st = gxa_next_image(desc, img);
			if (!wu_isok(st)) {
				break;
			}
			enum wu_error e = wuimg_alloc_limit(img, conf);
			if (e != wu_ok) {
				st = WUERR_HERE(e);
				break;
			}
			st = gxa_load_image(desc, img);
			if (!wu_isok(st)) {
				break;
			}
		}
	}
	return st;
}

static struct wu_st init_gxa(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	struct gxa_desc *desc = infile->dec_state;
	struct wu_st st = gxa_init(desc, infile->ifp);
	if (wu_isok(st)) {
		infile->nr = desc->nb_images;
		tree_add_leaf_len(&infile->metadata, "Comment",
			wuptr_mem(desc->comment, desc->comment_len), NULL);
	}
	return st;
}


static void end_bsi(struct image_file *infile) {
	bsi_cleanup(infile->dec_state);
}

static struct wu_st event_bsi(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *state, const enum image_event ev) {
	(void)_c;
	struct wuimg *img = infile->sub_img + state->idx;
	switch (ev) {
	case ev_metadata:
		return bsi_set_image(infile->dec_state, img);
	case ev_subcycle:
		return bsi_load_image(infile->dec_state, img,
			(uint16_t)state->idx);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_bsi(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	struct bsi_desc *desc = infile->dec_state;
	struct wu_st st = bsi_init(desc, infile->ifp);
	if (wu_isok(st)) {
		infile->nr = desc->nb_images;
		tree_add_leaf_utf8(&infile->metadata, "Compression",
			bsi_compression_str(desc->compression));
		tree_add_leaf_utf8(&infile->metadata, "Type",
			desc->bsif ? "BSIF" : "IFHD");
	}
	return st;
}

const struct image_fn fnhd_fn = {
	.state_size = sizeof(struct fnhd_desc),
	.init = init_fnhd,
	.event = event_fnhd,
	.end = end_fnhd,
};
const struct image_fn gxa_fn = {
	.state_size = sizeof(struct gxa_desc),
	.init = init_gxa,
	.event = event_gxa,
	.end = end_gxa,
};
const struct image_fn bsi_fn = {
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct bsi_desc),
	.init = init_bsi,
	.event = event_bsi,
	.end = end_bsi,
};
