#include <stdio.h>

#include "wudefs.h"
#include "common.h"

#include "lib_xbm.h"

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

	print_unsafe_data(desc.name, desc.name_len, "Source name", true,
		infile->meta.fp);
	if (desc.has_hotspot) {
		fprintf(infile->meta.fp, "Hot spot: %d %d\n",
			desc.x_hot, desc.y_hot);
	}

	if (umax(desc.w, desc.h) > wuconf->max_img_size) {
		xbm_cleanup(&desc);
		return wu_exceeded_size_limit;
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
