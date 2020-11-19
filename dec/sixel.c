#include <string.h>

#include "../wudefs.h"
#include "../common.h"
#include "lib/sixel.h"

enum wu_error sixel_dec(struct image_file *infile, const struct wu_conf *conf) {
	struct sixel_desc desc;
	enum lib_fail status = sixel_open_file(infile->ifp, &desc);
	if (status != lib_ok) {
		infile->err_msg = strdup(lib_fail_string(status));
		return wu_open_error;
	}

	status = sixel_calc_parameters(&desc);
	if (status != lib_ok) {
		sixel_cleanup(&desc);
		infile->err_msg = strdup(lib_fail_string(status));
		return wu_invalid_header;
	}

	if (zumax(desc.w, desc.h) > conf->max_img_size) {
		sixel_cleanup(&desc);
		return wu_exceeded_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	img->data = (unsigned char *)sixel_decode(&desc);
	sixel_cleanup(&desc);
	if (!img->data) {
		return wu_alloc_error;
	}
	img->w = desc.w;
	img->h = desc.h;
	img->channels = 4;
	img->bitdepth = 8;
	return wu_ok;
}
