// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <limits.h>

#include "wudefs.h"
#include "lib/pic2.h"

static void add_pic2_str(const char *name, const struct wuptr value,
struct wutree *tree) {
	tree_add_leaf_len(tree, name, value, "SHIFT_JIS");
}

static void read_pic2_metadata(const struct pic2_desc *desc, struct wutree *tree) {
	add_pic2_str("Name", desc->name, tree);
	add_pic2_str("Subtitle", desc->subtitle, tree);
	add_pic2_str("Title", desc->title, tree);
	add_pic2_str("Saver", desc->saver, tree);
	add_pic2_str("Comment", desc->comment, tree);

	tree_bud_leaf_time(tree, "Created", desc->created);
	tree_bud_leaf_u(tree, "Image number", desc->image_number);
	tree_bud_leaf_u(tree, "Canvas width", desc->w);
	tree_bud_leaf_u(tree, "Canvas height", desc->h);
	tree_bud_leaf_u(tree, "Depth", desc->depth);
}

static void read_pic2_block_metadata(const struct pic2_block *block,
struct wutree *tree) {
	if (tree) {
		tree_bud_leaf_u(tree, "X", block->u.image.x);
		tree_bud_leaf_u(tree, "Y", block->u.image.y);
		tree_add_leaf_utf8(tree, "Encoding", pic2_encoding_str(block->id));
	}
}

static struct wu_st init_pic2(struct image_file *infile) {
	struct pic2_desc desc;
	struct wu_st st = pic2_parse(&desc, infile->map);
	if (!wu_isok(st)) {
		return st;
	}

	read_pic2_metadata(&desc, &infile->metadata);

	size_t total = 0;
	size_t i = 0;
	struct pic2_block block;
	while (i < SHRT_MAX) {
		st = pic2_next_block(&desc, &block);
		if (st.st == wu_no_change) {
			if (!i) {
				return wuerr(wu_no_image_data,
					"no image blocks found");
			}
			break;
		} else if (st.st != wu_ok) {
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
		return WUERR_HERE(wu_alloc_error);
	}

	pic2_rewind(&desc);
	i = 0;
	while (i < infile->nr) {
		st = pic2_next_block(&desc, &block);
		if (st.st != wu_ok) {
			if (st.st == wu_no_change) {
				break;
			}
			continue;
		}
		if (block.is_image) {
			struct wuimg *img = infile->sub_img + i;
			st = pic2_set_image(&desc, &block, img);
			if (wu_isok(st)) {
				if (wuimg_alloc_limit(img, infile->conf) == wu_ok) {
					read_pic2_block_metadata(&block,
						wuimg_get_metadata(img));
					if (wu_isok(pic2_decode(&block, img))) {
						++i;
						continue;
					}
				}
			}
			wuimg_clear(img + i);
		}
	}
	return WUERR_CHECK(image_file_total_decoded(infile, i));
}

const struct image_fn pic2_fn = {
	.mmap = true,
	.init = init_pic2,
};
