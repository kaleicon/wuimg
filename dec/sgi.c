#include <stdio.h>
#include <string.h>

#include "../wudefs.h"
#include "../common.h"

#include "lib/sgi.h"

enum wu_error sgi_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	struct sgi_desc desc;
	enum lib_fail fail = sgi_open_file(infile->ifp, &desc);
	if (fail) {
		infile->err_msg = strdup(lib_fail_string(fail));
		return wu_invalid_signature;
	}

	fail = sgi_parse_header(&desc);
	if (fail) {
		infile->err_msg = strdup(lib_fail_string(fail));
		return wu_invalid_header;
	}

	tree_sprout_unsafe_leaf(&infile->metadata, "Image name", desc.name,
		sizeof(desc.name));

	if (umax(desc.w, desc.h) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		return wu_alloc_error;
	}

	img->data = sgi_decode(&desc);
	if (!img->data) {
		return wu_decoding_error;
	}
	img->w = desc.w;
	img->h = desc.h;
	if (desc.type == sgi_332) {
		img->channels = 3;
		img->bitdepth = rgb332;
	} else {
		img->channels = desc.ch;
		img->bitdepth = (unsigned char)(desc.bytedepth * 8);
	}
	img->mirror = true;
	return wu_ok;
}
