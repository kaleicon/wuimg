#include "wudefs.h"
#include "common.h"

#include "lib_avs.h"

enum wu_error avs_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	size_t w, h;
	const enum lib_fail status = avs_open_file(infile->ifp, &w, &h);
	if (status != lib_ok) {
		return wu_unexpected_eof;
	}

	if (zumax(w, h) > wuconf->max_img_size) {
		return wu_exceeded_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	img->w = w;
	img->h = h;
	img->channels = 4;
	img->bitdepth = 8;
	img->layout = argb;
	img->data = (unsigned char *)avs_load(infile->ifp, w, h);
	return img->data ? wu_ok : wu_alloc_error;
}
