#include "wudefs.h"
#include "common.h"

#include "lib_bmp.h"

enum wu_error bmp_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct bmp_desc desc;
	enum lib_fail fail = bmp_open_file(infile->ifp, &desc);
	if (fail) {
		return wu_open_error;
	}

	fail = bmp_parse_header(&desc);
	if (fail) {
		bmp_cleanup(&desc);
		return wu_invalid_header;
	}

	if (zumax(desc.w, desc.h) > wuconf->max_img_size) {
		bmp_cleanup(&desc);
		return wu_exceeded_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		bmp_cleanup(&desc);
		return wu_alloc_error;
	}


	img->palette = (unsigned char *)bmp_take_colormap(&desc);
	img->w = desc.w;
	img->h = desc.h;
	img->channels = desc.bitdepth == 32 ? 4 : 3;
	img->alignment = (unsigned char)bmp_get_row_alignment(&desc);
	img->layout = bgra;
	img->mirror = (desc.order == bmp_bottom_up);

	img->data = bmp_decode(&desc);
	bmp_cleanup(&desc);
	if (!img->data) {
		return wu_decoding_error;
	}

	if (img->palette) {
		img->bitdepth = desc.bitdepth;
	} else {
		img->bitdepth = 8;
	}

	return wu_ok;
}
