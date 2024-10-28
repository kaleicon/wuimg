// SPDX-License-Identifier: 0BSD
#include "rast_utils.h"
#include "lib/wgtspr.h"

static enum wu_error wrapper(struct image_file *infile,
const struct wu_conf *conf, struct wgtspr_desc *desc) {
	enum wu_error st = wgtspr_init(desc, infile->ifp);
	if (st != wu_ok) {
		return st;
	}

	if (!alloc_sub_images(infile, desc->sprites)) {
		return wu_alloc_error;
	}

	tree_bud_leaf_u(&infile->metadata, "Version", desc->version);
	tree_bud_leaf_u(&infile->metadata, "Slots", desc->sprites);

	size_t decoded = 0;
	size_t unused = 0;
	for (size_t i = 0; i < desc->sprites; ++i) {
		struct wuimg *img = infile->sub_img + decoded;
		st = wgtspr_next_sprite(desc, img);
		switch (st) {
		case wu_ok:
			if (!wuimg_exceeds_limit(img, conf)) {
				if (wgtspr_get_sprite(desc, img)) {
					++decoded;
					continue;
				}
			}
			break;
		case wu_no_change:
			++unused;
			break;
		default:
			break;
		}
		wuimg_clear(img);
	}
	return (unused == desc->sprites)
		? wu_no_image_data : image_file_total_decoded(infile, decoded);

}

static enum wu_error wgtspr_dec(struct image_file *infile,
const struct wu_conf *conf) {
	struct wgtspr_desc desc;
	const enum wu_error st = wrapper(infile, conf, &desc);
	wgtspr_cleanup(&desc);
	return st;
}

const struct image_fn wgtspr_fn = {
	.dec = wgtspr_dec,
};
