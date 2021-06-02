#include <string.h>

#include "../wudefs.h"
#include "../common.h"

#include "lib/pi.h"

enum wu_error pi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pi_desc desc;
	enum lib_fail fail = pi_open_file(infile->ifp, &desc);
	if (fail) {
		infile->err_msg = strdup(lib_fail_string(fail));
		return wu_invalid_signature;
	}

	fail = pi_read_header(&desc);
	if (fail) {
		pi_cleanup(&desc);
		infile->err_msg = strdup(lib_fail_string(fail));
		return wu_invalid_header;
	}

	if (desc.comment) {
		size_t len;
		if (desc.comment_area_len > desc.comment_len + 1) {
			len = desc.comment_area_len;
		} else {
			len = desc.comment_len;
		}
		tree_sprout_unsafe_leaf(&infile->metadata, "Comment",
			desc.comment, len);
	}
	tree_sprout_unsafe_leaf(&infile->metadata, "Saver model",
		desc.saver_sig, sizeof(desc.saver_sig));
	if (desc.saver) {
		tree_sprout_unsafe_leaf(&infile->metadata, "Saver data",
			desc.saver, desc.saver_len);
	}
	tree_bud_leaf(&infile->metadata, "Bitdepth", wu_leaf_unsigned,
		(union wu_leaf){.u = desc.bitdepth});

	if (zumax(desc.w, desc.h) > wuconf->max_img_size) {
		pi_cleanup(&desc);
		return wu_exceeds_size_limit;
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
	img->bitdepth = 8;
	return wu_ok;
}
