#include <string.h>

#include "../wudefs.h"
#include "../common.h"
#include "../rast_utils.h"
#include "../lib/pgx.h"

enum wu_error pgx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pgx_desc desc;
	enum lib_fail status = pgx_open_file(&desc, infile->ifp);
	if (status != lib_ok) {
		rast_error(infile, status);
		return wu_invalid_signature;
	}

	status = pgx_read_header(&desc);
	if (status != lib_ok) {
		rast_error(infile, status);
		return wu_invalid_header;
	}

	if (rast_exceeds_size(&desc.rast, wuconf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (img) {
		img->data = pgx_decode(&desc);
		if (img->data) {
			rast_to_raw(img, &desc.rast);
			img->disable_alpha = !desc.transparent;
			return wu_ok;
		}
		return wu_decoding_error;
	}
	return wu_alloc_error;
}
