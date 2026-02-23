// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "wudefs.h"
#include "lib/wgtspr.h"

static struct wu_st wrap_wgtspr_loop(struct image_file *infile,
struct wgtspr_desc *desc) {
	struct wu_st st = wgtspr_init(desc, infile->ifp);
	if (!wu_isok(st)) {
		return st;
	}

	if (!alloc_sub_images(infile, desc->sprites)) {
		return WUERR_HERE(wu_alloc_error);
	}

	tree_bud_leaf_u(&infile->metadata, "Version", desc->version);
	tree_bud_leaf_u(&infile->metadata, "Slots", desc->sprites);

	size_t decoded = 0;
	size_t unused = 0;
	for (size_t i = 0; i < desc->sprites; ++i) {
		struct wuimg *img = infile->sub_img + decoded;
		st = wgtspr_next_sprite(desc, img);
		switch (st.st) {
		case wu_ok:
			if (wuimg_alloc_limit(img, infile->conf) == wu_ok
			&& wu_isok(wgtspr_get_sprite(desc, img))) {
				++decoded;
				continue;
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
	return WUERR_CHECK((unused == desc->sprites)
		? wu_no_image_data : image_file_total_decoded(infile, decoded));

}

static struct wu_st init_wgtspr(struct image_file *infile) {
	struct wgtspr_desc desc;
	const struct wu_st st = wrap_wgtspr_loop(infile, &desc);
	wgtspr_cleanup(&desc);
	return st;
}

const struct image_fn wgtspr_fn = {
	.init = init_wgtspr,
};
