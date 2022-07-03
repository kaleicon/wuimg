#include <stdlib.h>
#include <string.h>

#include "wudefs.h"
#include "rast_utils.h"
#include "lib/sixel.h"

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *conf, const struct map_info *mm) {
	struct sixel_desc desc;
	enum wu_error st = sixel_open_mem(&desc, mm);
	if (st != wu_ok) {
		return st;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	st = sixel_calc_parameters(&desc, img);
	if (st != wu_ok) {
		return st;
	}

	if (raw_img_exceeds_limit(img, conf)) {
		return wu_exceeds_size_limit;
	}
	return sixel_decode(&desc, img) ? wu_ok : wu_decoding_error;
}

enum wu_error sixel_dec(struct image_file *infile, const struct wu_conf *conf) {
	return rast_map_wrap(infile, conf, decode);
}
