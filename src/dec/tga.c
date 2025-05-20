// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <ctype.h>

#include "../wudefs.h"

#include "../lib/tga.h"

static void read_extension_area(struct wutree *tree,
const struct tga_metadata *meta) {
	tree = tree_add_branch(tree, "Extension area");
	if (!tree) {
		return;
	}

	tree_add_leaf_len(tree, "Author name", WUPTR_ARRAY(meta->author.name),
		NULL);
	tree_add_leaf_len(tree, "Author comment", WUPTR_ARRAY(meta->author.comment),
		NULL);

	if (meta->timestamp) {
		tree_bud_leaf_time(tree, "Timestamp", meta->timestamp);
	}

	tree_add_leaf_len(tree, "Job ID", WUPTR_ARRAY(meta->job.name), NULL);

	if (meta->job.hour || meta->job.minute || meta->job.second) {
		const char fmt[] = "%.2hu:%.2hu:%.2hu";
		char buf[sizeof(fmt)];
		const size_t w =(size_t)snprintf(buf, sizeof(buf), fmt,
			meta->job.hour, meta->job.minute, meta->job.second);
		tree_add_leaf_utf8_len(tree, "Job time", wuptr_mem(buf, w));
	}

	tree_add_leaf_len(tree, "Software ID", WUPTR_ARRAY(meta->software.id),
		NULL);

	if (isgraph(meta->software.version_letter)) {
		tree_add_leaf_utf8_len(tree, "Software version letter",
			wuptr_mem(&meta->software.version_letter, 1));
	}
	if (meta->software.version_number) {
		tree_bud_leaf_u(tree, "Software version number",
			meta->software.version_number);
	}
}

static void read_tga_info(struct wutree *tree, const struct tga_desc *desc) {
	tree_add_leaf_utf8(tree, "Type", tga_type_str(desc->type));
	tree_add_leaf_len(tree, "ID", wuptr_mem(desc->meta.id, desc->meta.id_len),
		NULL);
	tree_bud_leaf_u(tree, "Depth", desc->depth);
	tree_bud_leaf_u(tree, "Map depth", desc->map.depth);
}

static enum wu_error dec_wrapper(struct image_file *infile,
const struct wu_conf *wuconf, struct tga_desc *desc) {
	struct wuimg *img = infile->sub_img;
	enum wu_error st = tga_parse_header(desc, img, infile->ifp);
	if (st) {
		return st;
	}

	read_tga_info(&infile->metadata, desc);
	bool extra_pal = (bool)desc->map.extra_pal;
	bool has_stamp = false;
	if (tga_parse_footer(desc, img)) {
		read_extension_area(&infile->metadata, &desc->meta);
		infile->bg = desc->meta.key_color;
		if (desc->meta.stamp_offset) {
			has_stamp = true;
		}
	}

	img = realloc_sub_images(infile, 1u + extra_pal + has_stamp);
	if (!img) {
		return wu_alloc_error;
	}

	size_t i = 1;
	if (has_stamp) {
		st = tga_parse_stamp(desc, img, img + i);
		if (st != wu_ok) {
			return st;
		}
		++i;
	}
	if (extra_pal) {
		struct palette *pal = tga_take_extra_palette(desc);
		img[i].data = (uint8_t *)pal;
		img[i].w = 16;
		img[i].h = 16;
		img[i].channels = 4;
		img[i].bitdepth = 8;
		memmove(img[i].data, pal->color, sizeof(pal->color));
	}
	for (i = 0; i < infile->nr; ++i) {
		if (wuimg_exceeds_limit(img + i, wuconf)) {
			return wu_exceeds_size_limit;
		}
	}

	if (!tga_decode(desc, img)) {
		return wu_decoding_error;
	}
	if (has_stamp) {
		if (!tga_decode_stamp(desc, img + 1)) {
			return wu_decoding_error;
		}
	}
	return wu_ok;
}

static enum wu_error tga_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct tga_desc desc;
	const enum wu_error err = dec_wrapper(infile, wuconf, &desc);
	tga_cleanup(&desc);
	return err;
}

const struct image_fn tga_fn = {
	.alloc_single = true,
	.dec = tga_dec
};
