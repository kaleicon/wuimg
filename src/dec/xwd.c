// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2023 kaleido
#include "lib/xwd.h"
#include "misc/common.h"
#include "wudefs.h"

static void get_metadata(const struct xwd_desc *desc, struct wutree *meta) {
	tree_add_leaf_utf8(meta, "Version", xwd_version_str(desc->version));
	tree_add_leaf_utf8(meta, "Format", xwd_format_str(desc->format));
	tree_add_leaf_utf8(meta, "Visual", xwd_visual_str(desc->visual));
	tree_add_leaf_utf8(meta, "Byte endian", endian_str(desc->byte_endian));
	tree_add_leaf_utf8(meta, "Bit endian", endian_str(desc->bit_endian));
	const struct wutree_sap pix[] = {
		{"Pixel size", {wu_leaf_unsigned, {.u = desc->bpp}}},
		{"Pixel depth", {wu_leaf_unsigned, {.u = desc->depth}}},
	};
	tree_bud_leaves(meta, pix, ARRAY_LEN(pix));

	struct wutree *win = tree_add_branch(meta, "Window");
	if (win) {
		const struct wutree_sap w[] = {
			{"W", {wu_leaf_unsigned, {.u = desc->win.w}}},
			{"H", {wu_leaf_unsigned, {.u = desc->win.h}}},
			{"X", {wu_leaf_unsigned, {.u = desc->win.x}}},
			{"Y", {wu_leaf_unsigned, {.u = desc->win.y}}},
			{"Border width", {wu_leaf_unsigned, {.u = desc->win.border_w}}},
		};
		tree_bud_leaves(win, w, ARRAY_LEN(w));
		tree_add_leaf_len(win, "Name", wuptr_wustr(desc->win.name), NULL);
	}
}

static void cleanup_xwd(struct image_file *infile) {
	xwd_cleanup(infile->dec_state);
}

static enum wu_error callback_xwd(struct image_file *infile,
const struct wu_conf *_c, struct wu_state *_s, const enum image_event ev) {
	(void)_c; (void)_s;
	if (ev == ev_subcycle) {
		return xwd_decode(infile->dec_state, infile->sub_img)
			? wu_ok : wu_decoding_error;
	}
	return wu_no_change;
}

static struct wu_st init_xwd(struct image_file *infile,
const struct wu_conf *conf) {
	struct wu_st st = xwd_open(infile->dec_state, infile->ifp);
	if (wu_isok(st)) {
		st = xwd_parse(infile->dec_state, infile->sub_img);
		if (wu_isok(st)) {
			get_metadata(infile->dec_state, &infile->metadata);
			st.st = wuimg_exceeds_limit(infile->sub_img, conf)
				? wu_exceeds_size_limit : wu_ok;
		}
	}
	return st;
}

const struct image_fn xwd_fn = {
	.alloc_single = true,
	.state_size = sizeof(struct xwd_desc),
	.init = init_xwd,
	.callback = callback_xwd,
	.end = cleanup_xwd,
};
