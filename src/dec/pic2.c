// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/pic2.h"

static void add_str(const char *name, const struct wuptr value,
struct wutree *tree) {
	tree_add_leaf_len(tree, name, value, "SHIFT_JIS");
}

static void read_metadata(const struct pic2_desc *desc, struct wutree *tree) {
	add_str("Name", desc->name, tree);
	add_str("Subtitle", desc->subtitle, tree);
	add_str("Title", desc->title, tree);
	add_str("Saver", desc->saver, tree);
	add_str("Comment", desc->comment, tree);

	tree_bud_leaf_time(tree, "Created", desc->created);
	tree_bud_leaf_u(tree, "Image number", desc->image_number);
	tree_bud_leaf_u(tree, "Canvas width", desc->w);
	tree_bud_leaf_u(tree, "Canvas height", desc->h);
	tree_bud_leaf_u(tree, "Depth", desc->depth);
}

static void read_block_metadata(const struct pic2_block *block,
struct wutree *tree) {
	if (tree) {
		tree_bud_leaf_u(tree, "X", block->u.image.x);
		tree_bud_leaf_u(tree, "Y", block->u.image.y);
		tree_add_leaf_utf8(tree, "Encoding", pic2_encoding_str(block->id));
	}
}

static enum wu_error pic2_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct pic2_desc desc;
	enum wu_error st = pic2_init(&desc, infile->map);
	if (st != wu_ok) {
		return st;
	}

	st = pic2_parse(&desc);
	if (st != wu_ok) {
		return st;
	}

	read_metadata(&desc, &infile->metadata);


	size_t total = 0;
	size_t i = 0;
	struct pic2_block block;
	while (i < SHRT_MAX) {
		st = pic2_next_block(&desc, &block);
		if (st == wu_no_change) {
			if (!i) {
				return wu_no_image_data;
			}
			break;
		} else if (st != wu_ok) {
			return st;
		}
		++total;
		i += block.is_image;
		if (block.id == pic2_pdpi) {
			tree_bud_leaf_u(&infile->metadata, "DPI", block.u.dpi);
		}
	}
	tree_bud_leaf_u(&infile->metadata, "Blocks", total);

	if (!alloc_sub_images(infile, i)) {
		return wu_alloc_error;
	}

	pic2_rewind(&desc);
	i = 0;
	while (i < infile->nr) {
		st = pic2_next_block(&desc, &block);
		if (st != wu_ok) {
			if (st == wu_no_change) {
				break;
			}
			continue;
		}
		if (block.is_image) {
			struct wuimg *img = infile->sub_img + i;
			st = pic2_set_image(&desc, &block, img);
			if (st == wu_ok) {
				if (!wuimg_exceeds_limit(img, wuconf)) {
					read_block_metadata(&block,
						wuimg_get_metadata(img));
					if (pic2_decode(&block, img)) {
						++i;
						continue;
					}
				}
			}
			wuimg_clear(img + i);
		}
	}
	return image_file_total_decoded(infile, i);
}

const struct image_fn pic2_fn = {.mmap = true, .dec = pic2_dec};
