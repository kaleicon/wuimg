// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "rast_utils.h"
#include "lib/tlg.h"

static struct wu_st init_tlg(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct tlg_desc desc;
	struct wu_st st = tlg_read_header(&desc, infile->map, infile->sub_img);
	if (wu_isok(st)) {
		enum wu_error e = wuimg_alloc_limit(infile->sub_img, wuconf);
		if (e == wu_ok) {
			tree_add_leaf_utf8(&infile->metadata, "Version",
				tlg_version_str(desc.version));
			st = tlg_decode(&desc, infile->sub_img);
		} else {
			st = WUERR_HERE(e);
		}
	}
	return st;
}

const struct image_fn tlg_fn = {
	.mmap = true,
	.alloc_single = true,
	.init = init_tlg,
};
