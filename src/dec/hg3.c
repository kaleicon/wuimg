#include "rast_utils.h"
#include "lib/hg3.h"

static enum wu_error decode(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm) {
	struct hg3_desc desc;
	enum wu_error st = hg3_open(&desc, mp_parser_mem(mm->len, mm->data));
	if (st != wu_ok) {
		return st;
	}

	size_t i = 0;
	while ((st = hg3_next_image(&desc)) == wu_ok) {
		struct raw_img *img = infile->sub_img;
		if (i >= infile->nr) {
			img = realloc_sub_images(infile, i + 1);
			if (!img) {
				break;
			}
		}
		img += i;

		st = hg3_parse_image(&desc, img);
		if (st == wu_ok) {
			if (!raw_img_exceeds_limit(img, wuconf)) {
				if (hg3_decode(&desc, img)) {
					++i;
				} else {
					raw_img_clear(img);
				}
			}
		}
	}
	return i ? image_file_total_decoded(infile, i) : st;
}

enum wu_error hg3_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return rast_map_wrap(infile, wuconf, decode);
}
