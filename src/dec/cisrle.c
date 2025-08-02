// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/cisrle.h"
#include "wudefs.h"

static struct wu_st init_cis(struct image_file *infile,
const struct wu_conf *conf) {
	struct cis_desc desc;
	struct wu_st st = cis_parse(&desc, infile->sub_img, infile->map,
		true);
	if (wu_isok(st)) {
		if (wuimg_exceeds_limit(infile->sub_img, conf)) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else {
			tree_add_leaf_utf8(&infile->metadata, "Resolution",
				cis_resolution_str(desc.res));
			st = cis_decode(&desc, infile->sub_img);
		}
	}
	return st;
}

const struct image_fn cisrle_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_cis,
};
