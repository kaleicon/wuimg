#include <string.h>

#include "../wudefs.h"
#include "../common.h"

#include "lib/sun.h"

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
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		sun_cleanup(&desc);
		return wu_alloc_error;
	}

	desc.expand = false;
	img->palette = (unsigned char *)sun_take_colormap(&desc);
	img->data = sun_decode(&desc);
	sun_cleanup(&desc);
	if (!img->data) {
		return wu_decoding_error;
	}

	img->w = desc.w;
	img->h = desc.h;
	img->channels = desc.ch;
	img->alignment = (unsigned char)sun_get_row_alignment(&desc);
	switch (desc.bitdepth) {
	case 32:
		img->channels = 4;
		img->true_channels = 3;
		// fallthrough
	case 24:
		if (desc.type != sun_rgb) {
			img->layout = bgra;
		}
		// fallthrough
	case 8:
		img->bitdepth = 8;
		break;
	default:
		img->bitdepth = desc.bitdepth;
		if (!img->palette) {
			img->attr |= pix_inverted;
		}
	}
	return wu_ok;
}
