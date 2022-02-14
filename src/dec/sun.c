#include "../wudefs.h"
#include "../rast_utils.h"
#include "../lib/sun.h"

static enum wu_error dec_wrap(struct image_file *infile,
const struct wu_conf *wuconf, struct sun_desc *desc) {
	const enum lib_fail fail = sun_parse_header(desc);
	if (fail) {
		rast_error(infile, fail);
		return wu_invalid_header;
	}

	const struct wu_leaf leaf = {
		.val.b = desc->type == sun_byte_encoded,
		.type = wu_leaf_bool,
	};
	tree_bud_leaf(&infile->metadata, "Compressed", leaf);

	if (rast_exceeds_size(&desc->rast, wuconf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img || !rast_to_raw_img(&desc->rast, img)) {
		return wu_alloc_error;
	}
	return sun_decode(desc, img->data) ? wu_ok : wu_decoding_error;
}

enum wu_error sun_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct sun_desc desc;
	const enum lib_fail fail = sun_open_file(&desc, infile->ifp);
	if (fail == lib_ok) {
		const enum wu_error err = dec_wrap(infile, wuconf, &desc);
		sun_cleanup(&desc);
		return err;
	}
	rast_error(infile, fail);
	return wu_open_error;
}
