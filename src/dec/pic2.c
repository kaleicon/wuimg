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

static bool stop_pic2_parse(const struct wu_st st) {
	return st.st == wu_no_change || st.st == wu_unexpected_eof;
}

static struct wu_st init_pic2(struct image_file *infile) {
	struct pic2_desc desc;
	struct wu_st st = pic2_parse(&desc, infile->map);
	if (!wu_isok(st)) {
		return st;
	}

	read_pic2_metadata(&desc, &infile->metadata);

	size_t total = 0;
	size_t nr = 0;
	struct pic2_block block;
	bool bad_data = false;
	while (total < (1u << 8)) {
		st = pic2_next_block(&desc, &block);
		if (stop_pic2_parse(st)) {
			break;
		} else if (st.st != wu_ok && !bad_data) {
			/* log only first error, more would be annoying
			 * and waste memory */
			bad_data = true;
			image_file_strerror_append(infile, st.msg);
		}
		++total;
		nr += block.is_image;
		if (block.id == pic2_pdpi) {
			tree_bud_leaf_u(&infile->metadata, "DPI", block.u.dpi);
		}
	}

	if (!nr) {
		return wuerr(wu_no_image_data, "no image blocks found");
	} else if (bad_data) {
		image_file_strerror_append(infile,
			"bad or unknown blocks were found. will ignore");
	}

	tree_bud_leaf_u(&infile->metadata, "Blocks", total);

	if (!alloc_sub_images(infile, nr)) {
		return WUERR_HERE(wu_alloc_error);
	}

	pic2_rewind(&desc);
	size_t i = 0;
	while (i < infile->nr) {
		st = pic2_next_block(&desc, &block);
		if (stop_pic2_parse(st)) {
			break;
		} else if (wu_isok(st) && block.is_image) {
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
			wuimg_clear(img);
		}
	}
	return WUERR_CHECK(image_file_total_decoded(infile, i));
}

const struct image_fn pic2_fn = {
	.mmap = true,
	.init = init_pic2,
};
