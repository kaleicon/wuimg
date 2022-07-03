#include "lib/qoi.h"
#include "rast_utils.h"

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm) {
	struct mp_parser mp;
	enum wu_error st = qoi_open(&mp, mm);
	if (st == wu_ok) {
		struct raw_img *img = alloc_sub_images(infile, 1);
		if (img) {
			st = qoi_parse(&mp, img);
			if (st == wu_ok) {
				if (!raw_img_exceeds_limit(img, wuconf)) {
					return qoi_decode(&mp, img)
						? wu_ok : wu_decoding_error;
				}
				return wu_exceeds_size_limit;
			}
			return st;
		}
		return wu_alloc_error;
	}
	return st;
}

enum wu_error qoi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return rast_map_wrap(infile, wuconf, dec_wrap);
}
