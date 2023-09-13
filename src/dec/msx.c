// SPDX-License-Identifier: 0BSD
#include "lib/msx.h"
#include "wudefs.h"

static enum wu_error sc_common(struct image_file *infile,
const struct wu_conf *wuconf, const enum msx_screen mode) {
	struct wuimg *img = alloc_sub_images(infile, 1);
	if (img) {
		struct msx_desc desc;
		const enum wu_error st = msx_parse(&desc, img, infile->ifp,
			mode);
		if (st == wu_ok) {
			if (desc.compressed) {
				tree_bud_leaf_bool(&infile->metadata,
					"Compressed", desc.compressed);
			}
			if (!wuimg_exceeds_limit(img, wuconf)) {
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
enum wu_error sc2_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return sc_common(infile, wuconf, msx_screen2);
}
enum wu_error sc3_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return sc_common(infile, wuconf, msx_screen3);
}
enum wu_error sc4_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return sc_common(infile, wuconf, msx_screen4);
}
enum wu_error sc5_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return sc_common(infile, wuconf, msx_screen5);
}
enum wu_error sc6_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return sc_common(infile, wuconf, msx_screen6);
}
enum wu_error sc7_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return sc_common(infile, wuconf, msx_screen7);
}
enum wu_error sc8_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return sc_common(infile, wuconf, msx_screen8);
}
enum wu_error sc10_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return sc_common(infile, wuconf, msx_screen10);
}
enum wu_error sc12_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return sc_common(infile, wuconf, msx_screen12);
}
const struct image_fn sc2_fn = {.dec = sc2_dec};
const struct image_fn sc3_fn = {.dec = sc3_dec};
const struct image_fn sc4_fn = {.dec = sc4_dec};
const struct image_fn sc5_fn = {.dec = sc5_dec};
const struct image_fn sc6_fn = {.dec = sc6_dec};
const struct image_fn sc7_fn = {.dec = sc7_dec};
const struct image_fn sc8_fn = {.dec = sc8_dec};
const struct image_fn sc10_fn = {.dec = sc10_dec};
const struct image_fn sc12_fn = {.dec = sc12_dec};
