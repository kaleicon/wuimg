// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "wudefs.h"
#include "lib/fax.h"

static struct wu_st init_g3(struct image_file *infile) {
	enum fax_coding std;
	enum endianness order;
	if (!g3_identify(infile->sub_img, infile->map, &std, &order)) {
		return wuerr(wu_unknown_file_type,
			"couldn't identify g3 stream parameters");
	}
	enum wu_error e = wuimg_alloc_limit(infile->sub_img, infile->conf);
	if (e == wu_ok) {
		tree_add_leaf_utf8(&infile->metadata, "Coding",
			fax_coding_str(std));
		tree_add_leaf_utf8(&infile->metadata, "Order",
			order == big_endian ? "MSB" : "LSB");
		return g3_decode(infile->sub_img, infile->map, std, order);
	}
	return WUERR_HERE(e);
}

/* ZyXEL fax */
static struct wu_st event_zyxel(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	struct wuptr *data = infile->dec_state;
	switch (ev) {
	case ev_metadata:
		return zyxel_parse(infile->sub_img, infile->map, data);
	case ev_subcycle:
		return zyxel_decode(infile->sub_img, *data);
	default: break;
	}
	return WU_NO_CHANGE;
}


/* QFX */
static struct wu_st event_qfx(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	const uint16_t idx = (uint16_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	switch (ev) {
	case ev_metadata: return qfx_set_page(infile->dec_state, img, idx);
	case ev_subcycle: return qfx_load_page(infile->dec_state, img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_qfx(struct image_file *infile) {
	struct qfx_desc *desc = infile->dec_state;
	struct wu_st st = qfx_parse(desc, infile->map);
	infile->nr = desc->nr_pages;
	return st;
}


/* IFF-FAXX */
static void read_faxx_metadata(const struct faxx_desc *desc,
struct wutree *tree) {
	tree_add_leaf_utf8(tree, "Coding", fax_coding_str(desc->compression));
	tree_bud_leaf_u(tree, "Line length (mm)", desc->line_mm);
	tree_bud_leaf_u(tree, "Vertical lines/mm", desc->vertical_res);
	if (desc->has_gphd) {
		tree_add_leaf_limit(tree, "ID", WUPTR_ARRAY(desc->gphd.id),
			NULL);
		tree_add_leaf_utf8(tree, "Page height",
			gphd_ph_str(desc->gphd.page_height));
		tree_bud_leaf_u(tree, "Page num", desc->gphd.page_num);
		tree_bud_leaf_u(tree, "Connection bitrate",
			desc->gphd.bitrate * GPHD_BITRATE_FACTOR);
		tree_bud_leaf_bool(tree, "Error correction",
			desc->gphd.error_correction);
		tree_bud_leaf_bool(tree, "Binary transfer",
			desc->gphd.binary_transfer);
	}
}

static struct wu_st event_faxx(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	(void)state;
	switch (ev) {
	case ev_metadata:
		faxx_set_image(infile->dec_state, infile->sub_img);
		read_faxx_metadata(infile->dec_state, &infile->metadata);
		return WU_OK;
	case ev_subcycle:
		return faxx_decode(infile->dec_state, infile->sub_img);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_faxx(struct image_file *infile) {
	return faxx_init(infile->dec_state, infile->map);
}


/* APF */
static struct wu_st event_apf(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct wu_st st = WU_NO_CHANGE;
	if (ev == ev_subcycle) {
		struct apf_desc *desc = infile->dec_state;
		const uint16_t idx = (uint16_t)state->idx;
		while (desc->cur_page <= idx) {
			struct wuimg *img = infile->sub_img + desc->cur_page;
			st = apf_next_page(desc, img);
			if (!wu_isok(st)) {
				break;
			}
			enum wu_error e = wuimg_alloc_limit(img, infile->conf);
			if (e != wu_ok) {
				st = WUERR_HERE(e);
				break;
			}
			st = apf_load_page(desc, img);
			if (!wu_isok(st)) {
				break;
			}
			struct wutree *meta = wuimg_get_metadata(img);
			if (meta) {
				tree_bud_leaf_bool(meta, "High resolution",
					desc->high_res);
			}
		}
	}
	return st;
}

static struct wu_st init_apf(struct image_file *infile) {
	struct apf_desc *desc = infile->dec_state;
	struct wu_st st = apf_parse(desc, infile->map);
	if (wu_isok(st)) {
		infile->nr = desc->nr_pages;
		tree_bud_leaf_time(&infile->metadata, "Timestamp",
			desc->timestamp);
		tree_add_leaf_limit(&infile->metadata, "Station ID",
			WUPTR_ARRAY(desc->station_id), NULL);
	}
	return st;
}

const struct image_fn g3_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_g3,
};

const struct image_fn zyxel_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct wuptr),
	.event = event_zyxel,
};
const struct image_fn qfx_fn = {
	.mmap = true,
	.state_size = sizeof(struct qfx_desc),
	.alloc_on_subcycle = true,
	.init = init_qfx,
	.event = event_qfx,
};
const struct image_fn faxx_fn = {
	.mmap = true,
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct faxx_desc),
	.init = init_faxx,
	.event = event_faxx,
};
const struct image_fn apf_fn = {
	.mmap = true,
	.state_size = sizeof(struct apf_desc),
	.init = init_apf,
	.event = event_apf,
};
