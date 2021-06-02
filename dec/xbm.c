#include <stdio.h>

#include "../wudefs.h"
#include "../common.h"

#include "lib/xbm.h"

enum wu_error xbm_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct xbm_desc desc;
	enum lib_fail fail = xbm_open_file(infile->ifp, &desc);
	if (fail) {
		return wu_unknown_file_type;
	}

	fail = xbm_read_header(&desc);
	if (fail) {
		xbm_cleanup(&desc);
		return wu_unknown_file_type;
	}

	tree_sprout_unsafe_leaf(&infile->metadata, "Source name", desc.name,
		desc.name_len);
	if (desc.has_hotspot) {
		char buf[sizeof(desc.x_hot) * 2 * 4];
		const size_t w = (size_t)sprintf(buf, "%d %d", desc.x_hot,
			desc.y_hot);
		tree_sprout_measured_leaf(&infile->metadata, "Hot spot",
			buf, w);
	}

	if (umax(desc.w, desc.h) > wuconf->max_img_size) {
		xbm_cleanup(&desc);
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		xbm_cleanup(&desc);
		return wu_alloc_error;
	}

	img->data = xbm_decode(&desc);
	xbm_cleanup(&desc);
	if (!img->data) {
		return wu_decoding_error;
	}

	img->w = desc.w;
	img->h = desc.h;
	img->channels = 1;
	img->bitdepth = 8;
	img->alignment = 8;
	return wu_ok;
}
