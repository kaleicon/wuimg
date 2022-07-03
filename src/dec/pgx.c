#include <string.h>

#include "wudefs.h"
#include "lib/pgx.h"

enum wu_error pgx_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct pgx_desc desc;
	enum wu_error st = pgx_open_file(&desc, infile->ifp);
	if (st != wu_ok) {
		return st;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	st = pgx_read_header(&desc, img);
	if (st != wu_ok) {
		return st;
	}

	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}
	return pgx_decode(&desc, img) ? wu_ok : wu_decoding_error;
}
