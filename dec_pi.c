#include <string.h>

#include "wudefs.h"
#include "common.h"

#include "lib_pi.h"

enum wu_error pi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pi_desc desc;
	enum pi_fail status = pi_open_file(infile->ifp, &desc);
	if (status != pi_ok) {
		infile->err_msg = strdup(pi_fail_string(status));
		return wu_invalid_signature;
	}

	status = pi_read_header(&desc);
	if (status != pi_ok) {
		pi_cleanup(&desc);
		infile->err_msg = strdup(pi_fail_string(status));
		return wu_invalid_header;
	}

	if (desc.comment) {
		size_t len;
		if (desc.comment_area_len > desc.comment_len + 1) {
			len = desc.comment_area_len;
		} else {
			len = desc.comment_len;
		}
		print_unsafe_data(desc.comment, len, "Comment", true);
	}
	print_unsafe_data(desc.saver_sig, sizeof(desc.saver_sig), "Saver model", true);
	if (desc.saver) {
		print_unsafe_data(desc.saver, desc.saver_len, "Saver data", true);
	}

	if (zumax(desc.w, desc.h) > wuconf->max_img_size) {
		pi_cleanup(&desc);
		return wu_exceeded_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		pi_cleanup(&desc);
		return wu_alloc_error;
	}

	img->palette = pi_take_palette(&desc);
	img->data = pi_decode(&desc);
	pi_cleanup(&desc);
	if (!img->data) {
		return wu_alloc_error;
	}

	img->w = desc.w;
	img->h = desc.h;
	img->channels = 4;
	img->bitdepth = desc.bitdepth;
	return wu_ok;
}
