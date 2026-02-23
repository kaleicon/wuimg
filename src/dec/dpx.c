// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "lib/dpx.h"
#include "misc/common.h"
#include "wudefs.h"

static void read_dpx_television(const struct dpx_desc *desc, struct wutree *tree) {
	const struct dpx_industry_television *t = &desc->industry.tv;
	char buf[16];
	const int w = snprintf(buf, sizeof(buf), "%x:%x:%x %x",
		t->time_code >> 24, (t->time_code >> 16) & 0xff,
		(t->time_code >> 8) & 0xff, t->time_code & 0xff);
	tree_add_leaf_utf8_len(tree, "Time code", wuptr_mem(buf, (size_t)w));

	const struct wutree_sap vid[] = {
		{"Interlaced", {wu_leaf_bool, {.f = t->interlaced}}},
		{"Field num.", {wu_leaf_unsigned, {.f = t->field}}},
		{"Horizontal sampling rate", {wu_leaf_float, {.f = t->horz_rate}}},
		{"Vertical sampling rate", {wu_leaf_float, {.f = t->vert_rate}}},
		{"Frame rate", {wu_leaf_float, {.f = t->frame_hz}}},
		{"Time offset", {wu_leaf_float, {.f = t->time_offset}}},
		{"Integration time", {wu_leaf_float, {.f = t->integration}}},
	};
	tree_bud_leaves(tree, vid, ARRAY_LEN(vid));
}

static void read_dpx_film(const struct dpx_desc *desc, struct wutree *tree) {
	const struct dpx_industry_film *f = &desc->industry.film;
	struct wutree *edge = tree_add_branch(tree, "Edge codes");
	if (edge) {
		const struct wutree_sap codes[] = {
			{"Manufacturer", {wu_leaf_unsigned, {.u = f->manufacturer}}},
			{"Type", {wu_leaf_unsigned, {.u = f->type}}},
			{"Perf offset", {wu_leaf_unsigned, {.u = f->perf_offset}}},
			{"Prefix", {wu_leaf_unsigned, {.u = f->prefix}}},
			{"Count", {wu_leaf_unsigned, {.u = f->count}}},
		};
		tree_bud_leaves(edge, codes, ARRAY_LEN(codes));
	}
	tree_add_leaf_limit(tree, "Format", WUPTR_ARRAY(f->format), NULL);

	const struct wutree_sap sap[] = {
		{"Frame num.", {wu_leaf_unsigned, {.u = f->frame_num}}},
		{"Total frames", {wu_leaf_unsigned, {.u = f->total_frames}}},
		{"Held count", {wu_leaf_unsigned, {.u = f->held_count}}},
		{"Frame rate", {wu_leaf_float, {.f = f->frame_rate}}},
		{"Shutter angle", {wu_leaf_float, {.f = f->shutter_angle}}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));

	tree_add_leaf_limit(tree, "Frame ID", WUPTR_ARRAY(f->frame_id), NULL);
	tree_add_leaf_limit(tree, "Slate info", WUPTR_ARRAY(f->slate_info), NULL);
}

static void read_dpx_industry(const struct dpx_desc *desc, struct wutree *tree) {
	struct wutree *b = tree_add_branch(tree, "Film");
	if (b) {
		read_dpx_film(desc, b);
	}

	b = tree_add_branch(tree, "Television");
	if (b) {
		read_dpx_television(desc, b);
	}
}

static void read_dpx_source(const struct dpx_desc *desc, struct wutree *tree) {
	const struct dpx_generic_source *s = &desc->generic.src;
	const struct wutree_sap sap[] = {
		{"X", {wu_leaf_unsigned, {.u = s->x}}},
		{"Y", {wu_leaf_unsigned, {.u = s->y}}},
		{"X center", {wu_leaf_float, {.f = s->x_center}}},
		{"Y center", {wu_leaf_float, {.f = s->y_center}}},
		{"W", {wu_leaf_unsigned, {.u = s->w}}},
		{"H", {wu_leaf_unsigned, {.u = s->h}}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));
	tree_add_leaf_limit(tree, "Name", WUPTR_ARRAY(s->filename), NULL);
	tree_bud_leaf_time(tree, "Created", s->date);
	tree_add_leaf_limit(tree, "Input device", WUPTR_ARRAY(s->input_device),
		NULL);
	tree_add_leaf_limit(tree, "Input serial number", WUPTR_ARRAY(s->input_sn),
		NULL);

	struct wutree *eros = tree_add_branch(tree, "Erosion");
	if (eros) {
		const struct wutree_sap frame[] = {
			{"Left", {wu_leaf_unsigned, {.u = s->erosion.left}}},
			{"Right", {wu_leaf_unsigned, {.u = s->erosion.right}}},
			{"Top", {wu_leaf_unsigned, {.u = s->erosion.top}}},
			{"Bottom", {wu_leaf_unsigned, {.u = s->erosion.bottom}}},
		};
		tree_bud_leaves(eros, frame, ARRAY_LEN(frame));
	}

	char buf[32];
	int w = snprintf(buf, sizeof(buf), "%.3g x %.3g mm", s->w_mm, s->h_mm);
	tree_add_leaf_utf8_len(tree, "Size", wuptr_mem(buf, (size_t)w));
}

static void read_file(const struct dpx_desc *desc, struct wutree *tree) {
	const struct dpx_generic_file *f = &desc->generic.file;
	tree_add_leaf_limit(tree, "Name", WUPTR_ARRAY(f->name), NULL);
	tree_bud_leaf_time(tree, "Created", f->date);
	tree_add_leaf_limit(tree, "Creator", WUPTR_ARRAY(f->creator), NULL);
	tree_add_leaf_limit(tree, "Project", WUPTR_ARRAY(f->project), NULL);
	tree_add_leaf_limit(tree, "Copyright", WUPTR_ARRAY(f->copyright), NULL);
}

static void read_dpx_generic(const struct dpx_desc *desc, struct wutree *tree) {
	struct wutree *b = tree_add_branch(tree, "File");
	if (b) {
		read_file(desc, b);
	}
	b = tree_add_branch(tree, "Source");
	if (b) {
		read_dpx_source(desc, b);
	}
}

static void read_dpx_metadata(const struct dpx_desc *desc, struct wutree *tree) {
	struct wutree *b = tree_add_branch(tree, "Generic");
	if (b) {
		read_dpx_generic(desc, b);
	}

	if (desc->has_industry) {
		b = tree_add_branch(tree, "Industry");
		if (b) {
			read_dpx_industry(desc, b);
		}
	}
}

static struct wu_st event_dpx(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	const uint8_t idx = (uint8_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	switch (ev) {
	case ev_metadata:
		return dpx_set_image(infile->dec_state, img, idx);
	case ev_subcycle:
		return dpx_decode(infile->dec_state, img, idx);
	default: break;
	}
	return WU_NO_CHANGE;
}

static struct wu_st init_dpx(struct image_file *infile) {
	struct dpx_desc *desc = infile->dec_state;
	struct wu_st st = dpx_parse(desc, infile->ifp);
	if (wu_isok(st)) {
		read_dpx_metadata(desc, &infile->metadata);
		infile->nr = desc->generic.image.nb_elem;
	}
	return st;
}

const struct image_fn dpx_fn = {
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct dpx_desc),
	.init = init_dpx,
	.event = event_dpx,
};
