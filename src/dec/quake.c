// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "lib/quake.h"
#include "rast_utils.h"
#include "wudefs.h"

static enum wu_error wrapper(struct image_file *infile,
const struct wu_conf *conf, struct idsp_desc *desc) {
	enum wu_error st = idsp_init(desc, infile->ifp);
	if (st != wu_ok) {
		return st;
	}

	struct wuimg *img = alloc_sub_images(infile, desc->frames);
	if (!img) {
		return wu_alloc_error;
	}

	struct wutree *tree = &infile->metadata;
	tree_bud_leaf_u(tree, "Version", desc->version);
	tree_add_leaf_utf8(tree, "Type", idsp_type_str(desc->type));
	if (desc->version == idsp_half_life) {
		tree_add_leaf_utf8(tree, "Alpha", idsp_alpha_str(desc->alpha));
	}
	tree_bud_leaf_u(tree, "Max width", desc->w);
	tree_bud_leaf_u(tree, "Max height", desc->h);
	tree_bud_leaf_f(tree, "Bounding radius", desc->radius);
	tree_bud_leaf_f(tree, "Beam length", desc->beam_length);
	tree_add_leaf_utf8(tree, "Synch", idsp_synch_str(desc->synch));

	uint64_t i = 0;
	while (i < desc->frames) {
		st = idsp_next_image(desc, img);
		if (st == wu_ok) {
			if (!wuimg_exceeds_limit(img, conf)) {
				if (idsp_read_image(desc, img)) {
					++img;
					++i;
					continue;
				}
				st = wu_decoding_error;
			} else {
				st = wu_exceeds_size_limit;
			}
		}
		image_file_error_append(infile, st);
		break;
	}
	return image_file_total_decoded(infile, i);
}

static enum wu_error idsp_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct idsp_desc desc;
	const enum wu_error st = wrapper(infile, conf, &desc);
	idsp_cleanup(&desc);
	return st;
}


static enum wu_error lmp_dec(struct image_file *infile,
const struct wu_conf *conf) {
	return rast_trivial_fread(infile, conf, lmp_init);
}

const struct image_fn idsp_fn = {
	.dec = idsp_dec,
};
const struct image_fn lmp_fn = {
	.alloc_single = true,
	.dec = lmp_dec,
};
