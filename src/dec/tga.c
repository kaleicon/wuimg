// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <ctype.h>
#include <string.h>

#include "wudefs.h"

#include "lib/tga.h"

static void end_tga(struct image_file *infile) {
	tga_cleanup(infile->dec_state);
}

static struct wu_st event_tga(struct image_file *infile,
struct wu_state *state, const enum image_event ev) {
	struct tga_desc *desc = infile->dec_state;
	struct wuimg *img = infile->sub_img + state->idx;
	switch (ev) {
	case ev_metadata:
		switch (state->idx) {
		case 0: return tga_img_info(desc, img);
		case 1:
			if (desc->meta.stamp_offset) {
				return tga_parse_stamp(desc, img);
			}
			// fallthrough
		case 2:
			img->w = 16;
			img->h = 16;
			img->channels = 4;
			img->bitdepth = 8;
			img->borrowed = true;
			return WU_OK;
		}
		break;
	case ev_subcycle:
		switch (state->idx) {
		case 0: return tga_decode(desc, img);
		case 1:
			if (desc->meta.stamp_offset) {
				return tga_load_stamp(desc, img);
			}
			// fallthrough
		case 2:
			img->data = (uint8_t *)desc->map.pal->color;
			return WU_OK;
		}
		break;
	default: break;
	}
	return WU_NO_CHANGE;
}

static void add_tga_ext_area(struct wutree *tree,
const struct tga_metadata *meta) {
	tree = tree_add_branch(tree, "Extension area");
	if (!tree) {
		return;
	}

	tree_add_leaf_limit(tree, "Author name",
		WUPTR_ARRAY(meta->author.name), NULL);
	tree_add_leaf_limit(tree, "Author comment",
		WUPTR_ARRAY(meta->author.comment), NULL);

	if (meta->timestamp) {
		tree_bud_leaf_time(tree, "Timestamp", meta->timestamp);
	}

	tree_add_leaf_limit(tree, "Job ID", WUPTR_ARRAY(meta->job.name), NULL);

	if (meta->job.hour || meta->job.minute || meta->job.second) {
		const char fmt[] = "%.2hu:%.2hu:%.2hu";
		char buf[sizeof(fmt)];
		const size_t w =(size_t)snprintf(buf, sizeof(buf), fmt,
			meta->job.hour, meta->job.minute, meta->job.second);
		tree_add_leaf_utf8_len(tree, "Job time", wuptr_mem(buf, w));
	}

	tree_add_leaf_limit(tree, "Software ID", WUPTR_ARRAY(meta->software.id),
		NULL);

	if (isgraph(meta->software.version_letter)) {
		tree_add_leaf_utf8_len(tree, "Software version letter",
			wuptr_mem(&meta->software.version_letter, 1));
	}
	if (meta->software.version_number) {
		tree_bud_leaf_u(tree, "Software version number",
			meta->software.version_number);
	}
	tree_add_leaf_utf8(tree, "Attr type", tga_attr_type_str(meta->attr));
}

static void read_tga_info(struct wutree *tree, const struct tga_desc *desc) {
	tree_add_leaf_utf8(tree, "Type", tga_type_str(desc->type));
	tree_add_leaf_len(tree, "ID", wuptr_mem(desc->meta.id, desc->meta.id_len),
		NULL);
	tree_bud_leaf_u(tree, "X", desc->x);
	tree_bud_leaf_u(tree, "Y", desc->y);
	tree_bud_leaf_u(tree, "Depth", desc->depth);
	tree_bud_leaf_u(tree, "Attr bits", desc->img_desc & 0xf);
	tree_bud_leaf_u(tree, "Map depth", desc->map.depth);
}

static struct wu_st init_tga(struct image_file *infile) {
	struct tga_desc *desc = infile->dec_state;
	struct wu_st st = tga_parse_header(desc, infile->ifp);
	if (!wu_isok(st)) {
		return st;
	}

	read_tga_info(&infile->metadata, desc);
	bool extra_pal = desc->map.pal && !desc->map.use;
	bool has_stamp = false;
	if (tga_parse_footer(desc)) {
		add_tga_ext_area(&infile->metadata, &desc->meta);
		infile->bg = desc->meta.key_color;
		if (desc->meta.stamp_offset) {
			has_stamp = true;
		}
	}
	infile->nr = 1u + has_stamp + extra_pal;
	return WU_OK;
}

const struct image_fn tga_fn = {
	.state_size = sizeof(struct tga_desc),
	.alloc_on_subcycle = true,
	.init = init_tga,
	.event = event_tga,
	.end = end_tga,
};
