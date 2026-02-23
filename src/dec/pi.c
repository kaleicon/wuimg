// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include "wudefs.h"
#include "lib/pi.h"

static void get_pi_metadata(const struct pi_desc *desc, struct wutree *tree) {
	if (!desc->lsp) {
		tree_add_leaf_len(tree, "Comment", desc->comm, "SHIFT-JIS");
		tree_add_leaf_len(tree, "Dummy", desc->dummy, NULL);
		tree_add_leaf_len(tree, "Saver model",
			WUPTR_ARRAY(desc->saver.model), "SHIFT-JIS");
		tree_add_leaf_len(tree, "Saver data",
			desc->saver.data, "SHIFT-JIS");
	}
	tree_bud_leaf_u(tree, "Depth", desc->depth);
}

static struct wu_st init_pi(struct image_file *infile) {
	struct pi_desc desc;
	struct wu_st st = pi_read_header(&desc, infile->sub_img, infile->map,
		infile->ext);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(infile->sub_img,
			infile->conf);
		if (e == wu_ok) {
			get_pi_metadata(&desc, &infile->metadata);
			st = pi_decode(&desc, infile->sub_img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}

static struct wu_st init_dpc(struct image_file *infile) {
	struct dpc_desc desc;
	struct wu_st st = dpc_read_header(&desc, infile->sub_img, infile->map);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(infile->sub_img,
			infile->conf);
		if (e == wu_ok) {
			if (desc.data.len) {
				tree_bud_leaf_u(&infile->metadata, "X", desc.x);
				tree_bud_leaf_u(&infile->metadata, "Y", desc.y);
			}
			st = dpc_decode(&desc, infile->sub_img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}

const struct image_fn pi_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_pi,
};
const struct image_fn dpc_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_dpc,
};
