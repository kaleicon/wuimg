#include <string.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/sun.h"

enum wu_error sun_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct sun_desc desc;
	enum lib_fail fail = sun_open_file(&desc, infile->ifp);
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

	const struct wu_leaf leaf = {
		.val.u = desc.type == sun_byte_encoded,
		.type = wu_leaf_unsigned
	};
	tree_bud_leaf(&infile->metadata, "Compression", leaf);

	if (rast_exceeds_size(&desc.rast, wuconf)) {
		sun_cleanup(&desc);
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		sun_cleanup(&desc);
		return wu_alloc_error;
	}

	rast_to_raw(img, &desc.rast);
	img->data = sun_decode(&desc);
	sun_cleanup(&desc);

	/* Comment away to interpret the unused byte in 32-bit files as alpha.
	 * Imagemagick does this. */
/*	if (img->channels == 4) {
		img->no_alpha = true;
	}*/
	return img->data ? wu_ok : wu_decoding_error;
}
