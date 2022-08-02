#include "wudefs.h"
#include "lib/msx.h"

static enum wu_error scr_common(struct image_file *infile,
const struct wu_conf *wuconf, const enum msx_screen mode) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (img) {
		struct msx_desc desc;
		const enum wu_error st = msx_parse(&desc, img, infile->ifp,
			mode);
		if (st == wu_ok) {
			if (!raw_img_exceeds_limit(img, wuconf)) {
				return msx_decode(&desc, img)
					? wu_ok : wu_decoding_error;
			}
			return wu_exceeds_size_limit;
		}
		return st;
	}
	return wu_alloc_error;
}

// Not my proudest achievement.
enum wu_error scr2_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return scr_common(infile, wuconf, msx_screen2);
}
enum wu_error scr3_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return scr_common(infile, wuconf, msx_screen3);
}
enum wu_error scr4_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return scr_common(infile, wuconf, msx_screen4);
}
enum wu_error scr5_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return scr_common(infile, wuconf, msx_screen5);
}
enum wu_error scr6_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return scr_common(infile, wuconf, msx_screen6);
}
enum wu_error scr7_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return scr_common(infile, wuconf, msx_screen7);
}
enum wu_error scr8_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return scr_common(infile, wuconf, msx_screen8);
}
enum wu_error scr10_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return scr_common(infile, wuconf, msx_screen10);
}
enum wu_error scr12_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return scr_common(infile, wuconf, msx_screen12);
}
