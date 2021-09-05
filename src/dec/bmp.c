#include "../wudefs.h"
#include "../common.h"

#include "../lib/bmp.h"

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
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		bmp_cleanup(&desc);
		return wu_alloc_error;
	}

	img->palette = bmp_take_colormap(&desc);
	img->data = bmp_decode(&desc);
	bmp_cleanup(&desc);
	if (!img->data) {
		return wu_decoding_error;
	}

	img->w = desc.w;
	img->h = desc.h;
	img->channels = bmp_has_alpha(&desc) ? 4 : 3;
	img->alignment = 4;
	img->layout = pix_bgra;
	img->mirror = (desc.order == bmp_bottom_up);
	if (img->palette) {
		img->bitdepth = desc.bitdepth;
	} else if (desc.bitdepth == 16 && desc.compression == bmp_no_compression) {
		img->bitdepth = pix_argb1555;
	} else {
		img->bitdepth = 8;
	}
	return wu_ok;
}
