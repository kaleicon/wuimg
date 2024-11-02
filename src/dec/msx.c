// SPDX-License-Identifier: 0BSD
#include "lib/msx.h"
#include "wudefs.h"

static enum wu_error msx_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct msx_desc desc;
	struct wuimg *img = infile->sub_img;
	const enum wu_error st = msx_parse(&desc, img, infile->ifp,
		infile->ext);
	if (st == wu_ok) {
		if (msx_mode_may_be_compressed(desc.mode)) {
			tree_bud_leaf_bool(&infile->metadata,
				"Compressed", desc.compressed);
		}
		if (msx_mode_may_have_alternate_field(desc.mode)) {
			tree_bud_leaf_bool(&infile->metadata,
				"Is alternate field", desc.is_alt_field);
		}
		if (!wuimg_exceeds_limit(img, conf)) {
			return msx_decode(&desc, img)
				? wu_ok : wu_decoding_error;
		}
		return wu_exceeds_size_limit;
	}
	return st;
}

const struct image_fn msx_fn = {
	.alloc_single = true,
	.dec = msx_dec,
};
