#include "wudefs.h"
#include "lib/dpx.h"

static void read_television(const struct dpx_desc *desc, struct wu_tree *tree) {
	const struct dpx_industry_television *t = &desc->industry.tv;
	char buf[16];
	int w = snprintf(buf, sizeof(buf), "%x:%x:%x %x",
		t->time_code >> 24, (t->time_code >> 16) & 0xff,
		(t->time_code >> 8) & 0xff, t->time_code & 0xff);
	if (w > 0) {
		tree_add_leaf_utf8_len(tree, "Time code", buf, (size_t)w);
	}

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
	tree_add_leaf_limit(tree, "Format", f->format, sizeof(f->format), NULL);

	const struct wu_tree_sap sap[] = {
		{"Frame num.", {wu_leaf_unsigned, {.u = f->frame_num}}},
		{"Total frames", {wu_leaf_unsigned, {.u = f->total_frames}}},
		{"Held count", {wu_leaf_unsigned, {.u = f->held_count}}},
		{"Frame rate", {wu_leaf_float, {.f = f->frame_rate}}},
		{"Shutter angle", {wu_leaf_float, {.f = f->shutter_angle}}},
	};
	tree_bud_leaves(tree, sap, ARRAY_LEN(sap));

	tree_add_leaf_len(tree, "Frame ID", f->frame_id, sizeof(f->frame_id),
		NULL);
	tree_add_leaf_len(tree, "Slate info", f->slate_info,
		sizeof(f->slate_info), NULL);
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
	tree_add_leaf_len(tree, "Name", s->filename, sizeof(s->filename), NULL);
	const struct wu_leaf leaf = {.type = wu_leaf_time, .val.time = s->date};
	tree_bud_leaf(tree, "Created", leaf);
	tree_add_leaf_len(tree, "Input device", s->input_device,
		sizeof(s->input_device), NULL);
	tree_add_leaf_len(tree, "Input serial number", s->input_sn,
		sizeof(s->input_sn), NULL);

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

	char buf[sizeof(s->horz_aspect) * 2 * 3];
	int w = snprintf(buf, sizeof(buf), "%u:%u", s->horz_aspect, s->vert_aspect);
	if (w > 0) {
		tree_add_leaf_utf8_len(tree, "Aspect ratio", buf, (size_t)w);
	}

	w = snprintf(buf, sizeof(buf), "%.3g x %.3g mm", s->w_mm, s->h_mm);
	if (w > 0) {
		tree_add_leaf_utf8_len(tree, "Size", buf, (size_t)w);
	}
}

static void read_file(const struct dpx_desc *desc, struct wu_tree *tree) {
	const struct dpx_generic_file *f = &desc->generic.file;
	tree_add_leaf_len(tree, "Name", f->name, sizeof(f->name), NULL);
	const struct wu_leaf leaf = {.type = wu_leaf_time, .val.time = f->date};
	tree_bud_leaf(tree, "Created", leaf);
	tree_add_leaf_len(tree, "Creator", f->creator, sizeof(f->creator), NULL);
	tree_add_leaf_len(tree, "Project", f->project, sizeof(f->project), NULL);
	tree_add_leaf_len(tree, "Copyright", f->copyright,
		sizeof(f->copyright), NULL);
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

enum wu_error dpx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct dpx_desc desc;
	enum wu_error st = dpx_open(&desc, infile->ifp);
	if (st != wu_ok) {
		return st;
	}

	st = dpx_parse(&desc);
	if (st != wu_ok) {
		return st;
	}

	read_metadata(&desc, &infile->metadata);

	if (!alloc_sub_images(infile, desc.generic.image.nb_elem)) {
		return wu_alloc_error;
	}

	uint8_t o = 0;
	for (uint8_t i = 0; i < infile->nr; ++i) {
		struct raw_img *img = infile->sub_img + o;
		if (dpx_set_image(&desc, img, i) == wu_ok
		&& !raw_img_exceeds_limit(img, wuconf)
		&& dpx_decode(&desc, img, i)) {
			++o;
		} else {
			raw_img_clear(img);
		}
	}
	return image_file_total_decoded(infile, o);
}
