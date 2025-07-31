// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/nokia.h"
#include "wudefs.h"

static struct wu_st init_nol(struct image_file *infile,
const struct wu_conf *conf) {
	struct nol_desc desc;
	struct wu_st st = nol_parse(&desc, infile->sub_img, infile->ifp);
	if (wu_isok(st)) {
		if (wuimg_exceeds_limit(infile->sub_img, conf)) {
			st = WUERR_HERE(wu_exceeds_size_limit);
		} else {
			struct wutree *t = &infile->metadata;
			tree_add_leaf_utf8(t, "Type",
				desc.is_nol ? "NOL" : "NGG");
			tree_bud_leaf_u(t, "Mystery number", desc.mystery);
			if (desc.is_nol) {
				tree_bud_leaf_u(t, "Country code", desc.country);
				tree_bud_leaf_u(t, "Network code", desc.network);
			}
			st = nol_load(&desc, infile->sub_img);
		}
	}
	return st;
}

const struct image_fn nol_fn = {
	.alloc_single = true,
	.init = init_nol,
};
