// SPDX-License-Identifier: 0BSD
#include "lib/dpx.h"
#include "wudefs.h"

static void read_television(const struct dpx_desc *desc, struct wu_tree *tree) {
	const struct dpx_industry_television *t = &desc->industry.tv;
	char buf[16];
	const int w = snprintf(buf, sizeof(buf), "%x:%x:%x %x",
		t->time_code >> 24, (t->time_code >> 16) & 0xff,
		(t->time_code >> 8) & 0xff, t->time_code & 0xff);
	tree_add_leaf_utf8_len(tree, "Time code", wuptr_mem(buf, (size_t)w));

	const struct wu_tree_sap vid[] = {
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

static void read_film(const struct dpx_desc *desc, struct wu_tree *tree) {
	const struct dpx_industry_film *f = &desc->industry.film;
	struct wu_tree *edge = tree_add_branch(tree, "Edge codes");
	if (edge) {
		const struct wu_tree_sap codes[] = {
			{"Manufacturer", {wu_leaf_unsigned, {.u = f->manufacturer}}},
			{"Type", {wu_leaf_unsigned, {.u = f->type}}},
			{"Perf offset", {wu_leaf_unsigned, {.u = f->perf_offset}}},
			{"Prefix", {wu_leaf_unsigned, {.u = f->prefix}}},
			{"Count", {wu_leaf_unsigned, {.u = f->count}}},
		};
		tree_bud_leaves(edge, codes, ARRAY_LEN(codes));
	}
	tree_add_leaf_limit(tree, "Format", WUPTR_ARRAY(f->format), NULL);

	const struct wu_tree_sap sap[] = {
		{"Frame num.", {wu_leaf_unsigned, {.u = f->frame_num}}},
		{"Total frames", {wu_leaf_unsigned, {.u = f->total_frames}}},
		{"Held count", {wu_leaf_unsigned, {.u = f->held_count}}},
		{"Frame rate", {wu_leaf_float, {.f = f->frame_rate}}},
		{"Shutter angle", {wu_leaf_float, {.f = f->shutter_angle}}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));

	tree_add_leaf_len(tree, "Frame ID", WUPTR_ARRAY(f->frame_id), NULL);
	tree_add_leaf_len(tree, "Slate info", WUPTR_ARRAY(f->slate_info), NULL);
}

static void read_industry(const struct dpx_desc *desc, struct wu_tree *tree) {
	struct wu_tree *b = tree_add_branch(tree, "Film");
	if (b) {
		read_film(desc, b);
	}

	b = tree_add_branch(tree, "Television");
	if (b) {
		read_television(desc, b);
	}
}

static void read_source(const struct dpx_desc *desc, struct wu_tree *tree) {
	const struct dpx_generic_source *s = &desc->generic.src;
	const struct wu_tree_sap sap[] = {
		{"X", {wu_leaf_unsigned, {.u = s->x}}},
		{"Y", {wu_leaf_unsigned, {.u = s->y}}},
		{"X center", {wu_leaf_float, {.f = s->x_center}}},
		{"Y center", {wu_leaf_float, {.f = s->y_center}}},
		{"W", {wu_leaf_unsigned, {.u = s->w}}},
		{"H", {wu_leaf_unsigned, {.u = s->h}}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));
	tree_add_leaf_len(tree, "Name", WUPTR_ARRAY(s->filename), NULL);
	tree_bud_leaf_time(tree, "Created", s->date);
	tree_add_leaf_len(tree, "Input device", WUPTR_ARRAY(s->input_device),
		NULL);
	tree_add_leaf_len(tree, "Input serial number", WUPTR_ARRAY(s->input_sn),
		NULL);

	struct wu_tree *eros = tree_add_branch(tree, "Erosion");
	if (eros) {
		const struct wu_tree_sap frame[] = {
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

static void read_file(const struct dpx_desc *desc, struct wu_tree *tree) {
	const struct dpx_generic_file *f = &desc->generic.file;
	tree_add_leaf_len(tree, "Name", WUPTR_ARRAY(f->name), NULL);
	tree_bud_leaf_time(tree, "Created", f->date);
	tree_add_leaf_len(tree, "Creator", WUPTR_ARRAY(f->creator), NULL);
	tree_add_leaf_len(tree, "Project", WUPTR_ARRAY(f->project), NULL);
	tree_add_leaf_len(tree, "Copyright", WUPTR_ARRAY(f->copyright), NULL);
}

static void read_generic(const struct dpx_desc *desc, struct wu_tree *tree) {
	struct wu_tree *b = tree_add_branch(tree, "File");
	if (b) {
		read_file(desc, b);
	}
	b = tree_add_branch(tree, "Source");
	if (b) {
		read_source(desc, b);
	}
}

static void read_metadata(const struct dpx_desc *desc, struct wu_tree *tree) {
	struct wu_tree *b = tree_add_branch(tree, "Generic");
	if (b) {
		read_generic(desc, b);
	}

	if (desc->has_industry) {
		b = tree_add_branch(tree, "Industry");
		if (b) {
			read_industry(desc, b);
		}
	}
}

static enum wu_error dec_wrap(struct wuimg *img, const struct wu_conf *wuconf,
const struct dpx_desc *desc, const uint8_t idx) {
	const enum wu_error st =  dpx_set_image(desc, img, idx);
	if (st == wu_ok) {
		if (!wuimg_exceeds_limit(img, wuconf)) {
			return dpx_decode(desc, img, idx)
				? wu_ok : wu_decoding_error;
		}
		return wu_exceeds_size_limit;
	}
	return st;
}

static enum wu_error dpx_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, const enum image_event ev) {
	(void)ev;
	const uint8_t idx = (uint8_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	return dec_wrap(img, wuconf, infile->dec_state, idx);
}

static enum wu_error dpx_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	(void)wuconf;
	struct dpx_desc *desc = malloc(sizeof(*desc));
	if (desc) {
		infile->dec_state = desc;
		enum wu_error st = dpx_open(desc, infile->ifp);
		if (st == wu_ok) {
			st = dpx_parse(desc);
			if (st == wu_ok) {
				read_metadata(desc, &infile->metadata);
				return alloc_sub_images(infile, desc->generic.image.nb_elem)
					? wu_ok : wu_alloc_error;
			}
		}
		return st;
	}
	return wu_alloc_error;
}

const struct image_fn dpx_fn = {
	.dec = dpx_dec,
	.callback = dpx_callback,
	.end = image_file_end,
};
