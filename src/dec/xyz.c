#include "rast_utils.h"
#include "lib/xyz.h"

enum wu_error xyz_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	(void)wuconf; (void)state; (void)ev;
	xyz_free(infile->dec_state, infile->sub_img);
	return wu_ok;
}

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm) {
	struct xyz_desc *desc = malloc(sizeof(*desc));
	if (desc) {
		infile->dec_state = desc;
		enum wu_error st = xyz_open(desc, mp_parser_mem(mm->len, mm->data));
		if (st == wu_ok) {
			struct raw_img *img = alloc_sub_images(infile, 1);
			if (img) {
				st = xyz_parse(desc, img);
				if (st == wu_ok) {
					if (!raw_img_exceeds_limit(img, wuconf)) {
						return xyz_decode(desc, img)
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
	return wu_alloc_error;
}

enum wu_error xyz_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return rast_map_wrap(infile, wuconf, dec_wrap);
}
