// SPDX-License-Identifier: 0BSD
#include "lib/xwd.h"
#include "misc/common.h"
#include "rast_utils.h"

static void meta(const void *restrict ptr, struct wu_tree *meta) {
	const struct xwd_desc *desc = ptr;
	tree_add_leaf_utf8(meta, "Version", xwd_version_str(desc->version));
	tree_add_leaf_utf8(meta, "Format", xwd_format_str(desc->format));
	tree_add_leaf_utf8(meta, "Visual", xwd_visual_str(desc->visual));
	tree_add_leaf_utf8(meta, "Byte endian", endian_str(desc->byte_endian));
	tree_add_leaf_utf8(meta, "Bit endian", endian_str(desc->bit_endian));
	const struct wu_tree_sap pix[] = {
		{"Pixel size", {wu_leaf_unsigned, {.u = desc->bpp}}},
		{"Pixel depth", {wu_leaf_unsigned, {.u = desc->depth}}},
	};
	tree_bud_leaves(meta, pix, ARRAY_LEN(pix));

	struct wu_tree *win = tree_add_branch(meta, "Window");
	if (win) {
		const struct wu_tree_sap w[] = {
			{"W", {wu_leaf_unsigned, {.u = desc->win.w}}},
			{"H", {wu_leaf_unsigned, {.u = desc->win.h}}},
			{"X", {wu_leaf_unsigned, {.u = desc->win.x}}},
			{"Y", {wu_leaf_unsigned, {.u = desc->win.y}}},
			{"Border width", {wu_leaf_unsigned, {.u = desc->win.border_w}}},
		};
		tree_bud_leaves(win, w, ARRAY_LEN(w));
		if (desc->win.name.len) {
			tree_add_leaf_len(win, "Name", wuptr_wustr(desc->win.name), NULL);
		}
	}
}

static void cleanup(void *desc) {
	xwd_cleanup(desc);
}

static size_t dec(const void *restrict desc, struct wuimg *img) {
	return xwd_decode(desc, img);
}

static enum wu_error parse(void *restrict desc, struct wuimg *img) {
	return xwd_parse(desc, img);
}

static enum wu_error open(void *restrict desc, struct image_file *infile) {
	return xwd_open(desc, infile->ifp);
}

static enum wu_error xwd_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct xwd_desc desc;
	return rast_trivial_dec(infile, wuconf, &desc, open, parse, meta, dec,
		cleanup);
}

const struct image_fn xwd_fn = {.mmap = false, .dec = xwd_dec};
