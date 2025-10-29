// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <ctype.h>

#include "lib/pcx.h"
#include "misc/common.h"
#include "wudefs.h"

static size_t is_readable_garbage(const unsigned char *data, const size_t len) {
	size_t i = 0;
	while (i < len) {
		if (!data[i]) {
			break;
		} else if (!isprint(data[i])) {
			return 0;
		}
		++i;
	}
	return i;
}

static void add_metadata(const struct pcx_desc *desc, struct wuimg *img) {
	struct wutree *metadata = wuimg_get_metadata(img);
	if (!metadata) {
		return;
	}

	tree_add_leaf_utf8(metadata, "Format version",
		pcx_version_string(desc->version));

	const struct wutree_sap sap[] = {
		{"Planes", {wu_leaf_unsigned, {.u = img->channels}}},
		{"Bitdepth", {wu_leaf_unsigned, {.u = img->bitdepth}}},
		{"XStart", {wu_leaf_unsigned, {.u = desc->xstart}}},
		{"YStart", {wu_leaf_unsigned, {.u = desc->ystart}}},
		{"XEnd", {wu_leaf_unsigned, {.u = desc->xend}}},
		{"YEnd", {wu_leaf_unsigned, {.u = desc->yend}}},
		{"Palette mode", {wu_leaf_unsigned, {.u = desc->palette_type}}},
		{"Horizontal resolution", {wu_leaf_unsigned, {.u = desc->horz_res}}},
		{"Vertical resolution", {wu_leaf_unsigned, {.u = desc->vert_res}}},
		{"Horizontal screen size", {wu_leaf_unsigned, {.u = desc->horz_screen}}},
		{"Vertical screen size", {wu_leaf_unsigned, {.u = desc->vert_screen}}},
	};
	const bool scrsize = desc->horz_screen || desc->vert_screen;
	tree_bud_leaves(metadata, sap, ARRAY_LEN(sap) - (scrsize ? 0 : 2));

	if (desc->entries <= 4) {
		const void *garbage = desc->file_pal + 12;
		const size_t len = is_readable_garbage(garbage,
			sizeof(desc->file_pal) - 12);
		if (len > 3) {
			tree_add_leaf_len(metadata, "Garbage",
				wuptr_mem(garbage, len), NULL);
		}
	}
}

static struct wu_st common_pcx(struct pcx_desc *desc, struct wuimg *img,
const struct wu_conf *wuconf) {
	if (wuimg_exceeds_limit(img, wuconf)) {
		return WUERR_HERE(wu_exceeds_size_limit);
	}
	add_metadata(desc, img);
	return pcx_decode(desc, img);
}

static struct wu_st init_pcx(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct pcx_desc desc;
	struct wu_st st = pcx_read_header(&desc, infile->sub_img, infile->map,
		true);
	if (wu_isok(st)) {
		st = common_pcx(&desc, infile->sub_img, wuconf);
	}
	return st;
}


static struct wu_st event_dcx(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	if (ev == ev_subcycle) {
		struct dcx_desc *desc = infile->dec_state;
		const uint32_t i = (uint32_t)state->idx;
		struct wuimg *img = infile->sub_img + i;

		struct pcx_desc pcx;
		struct wu_st st = dcx_set_file(desc, &pcx, img, i);
		if (wu_isok(st)) {
			st = common_pcx(&pcx, img, wuconf);
		}
		return st;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_dcx(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct dcx_desc *desc = infile->dec_state;
	const struct wu_st st = dcx_open_file(desc, infile->map);
	if (wu_isok(st)) {
		infile->nr = desc->nr;
	}
	return st;
}

const struct image_fn pcx_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_pcx,
};
const struct image_fn dcx_fn = {
	.mmap = true,
	.state_size = sizeof(struct dcx_desc),
	.init = init_dcx,
	.event = event_dcx,
};
