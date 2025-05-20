// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/eclipse.h"
#include "wudefs.h"

static struct wu_st init_eclipse(struct image_file *infile,
const struct wu_conf *conf) {
	struct eclipse_desc desc;
	struct wuimg *img = infile->sub_img;
	struct wu_st st = eclipse_init(&desc, img, infile->ifp);
	if (wu_isok(st)) {
		tree_bud_leaf_u(&infile->metadata, "Version", desc.version);
		tree_add_leaf_utf8_limit(&infile->metadata, "Software",
			wuptr_mem(desc.software, sizeof(desc.software)));
		tree_add_leaf_utf8_limit(&infile->metadata, "Revision",
			wuptr_mem(desc.revision, sizeof(desc.revision)));
		if (wuimg_exceeds_limit(img, conf)) {
			return WUERR_HERE(wu_exceeds_size_limit);
		}
		st = eclipse_load(&desc, img);
	}
	return st;
}

const struct image_fn eclipse_fn = {
	.alloc_single = true,
	.init = init_eclipse,
};
