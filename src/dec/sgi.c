#include <string.h>

#include "../wudefs.h"
#include "../rast_utils.h"
#include "../lib/sgi.h"

enum wu_error sgi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct sgi_desc desc;
	enum lib_fail fail = sgi_open_file(&desc, infile->ifp);
	if (fail) {
		rast_error(infile, fail);
		return wu_invalid_signature;
	}

	fail = sgi_parse_header(&desc);
	if (fail) {
		rast_error(infile, fail);
		return wu_invalid_header;
	}

	if (strnlen(desc.name, sizeof(desc.name))) {
		tree_sprout_unsafe_leaf(&infile->metadata, "Image name",
			desc.name, sizeof(desc.name));
	}
	tree_bud_leaf(&infile->metadata, "Compressed",
		(struct wu_leaf){.val.b = (desc.compression != sgi_uncompressed),
			.type = wu_leaf_bool});

	if (rast_exceeds_size(&desc.rast, wuconf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (img && rast_to_raw_img(&desc.rast, img)) {
		img->mirror = true;
		return sgi_decode(&desc, img->data) ? wu_ok : wu_decoding_error;
	}
	return wu_alloc_error;

/*	img->data = sgi_decode(&desc);
	if (img->data) {
		rast_to_raw(img, &desc.rast);
		img->mirror = true;
		return wu_ok;
	}
	return wu_decoding_error;*/
}
