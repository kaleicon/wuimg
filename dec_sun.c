#include <string.h>

#include "wudefs.h"
#include "common.h"

#include "lib_sun.h"

enum wu_error sun_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct sun_desc desc;
	enum lib_fail fail = sun_open_file(infile->ifp, &desc);
	if (fail) {
		infile->err_msg = strdup(lib_fail_string(fail));
		return wu_open_error;
	}

	fail = sun_parse_header(&desc);
	if (fail) {
		sun_cleanup(&desc);
		infile->err_msg = strdup(lib_fail_string(fail));
		return wu_invalid_header;
	}

	if (zumax(desc.w, desc.h) > wuconf->max_img_size) {
		sun_cleanup(&desc);
		return wu_exceeded_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		sun_cleanup(&desc);
		return wu_alloc_error;
	}

	img->palette = (unsigned char *)sun_take_colormap(&desc);
	img->w = desc.w;
	img->h = desc.h;
	img->channels = desc.ch;
	img->alignment = (unsigned char)sun_get_row_alignment(&desc);
	if (desc.bitdepth >= 24) {
		if (desc.type != sun_rgb) {
			img->layout = bgra;
		}
		if (desc.bitdepth == 32) {
			img->channels = 4;
			img->true_channels = 3;
		}
	}

	img->data = sun_decode(&desc);
	sun_cleanup(&desc);
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
