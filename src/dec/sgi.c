#include <stdio.h>
#include <string.h>

#include "../wudefs.h"
#include "../common.h"
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
	tree_bud_leaf(&infile->metadata, "Compression",
		(struct wu_leaf){.val.u = desc.compression, .type = wu_leaf_unsigned});

	if (rast_exceeds_size(&desc.rast, wuconf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	img->data = sgi_decode(&desc);
	if (img->data) {
		rast_to_raw(img, &desc.rast);
		img->mirror = true;
		return wu_ok;
	}
	return wu_decoding_error;
}
