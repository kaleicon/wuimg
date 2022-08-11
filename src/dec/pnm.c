#include <limits.h>

#include "wudefs.h"
#include "common.h"
#include "lib/pnm.h"

static bool rast_to_raw_img(struct raster_desc *desc, struct raw_img *img) {
	img->w = desc->w;
	img->h = desc->h;
	img->channels = desc->ch;
	img->bitdepth = desc->bitdepth;
	img->layout = desc->layout;
	img->attr = desc->attr;
	img->mirror = desc->mirror;
	return raw_img_alloc(img);
}

enum wu_error pnm_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct pnm_desc desc;
	enum wu_error st = pnm_open_file(&desc, infile->ifp, true);
	if (st != wu_ok) {
		return st;
	}

	st = pnm_parse_header(&desc);
	if (st) {
		return st;
	}

	if (zumax(desc.rast.w, desc.rast.h) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}

	tree_add_leaf_utf8(&infile->metadata, "Type", pnm_type_str(desc.type));

	if (!alloc_sub_images(infile, zumin(desc.nr, UCHAR_MAX))) {
		return wu_alloc_error;
	}

	size_t i = 0;
	while (i < infile->nr) {
		struct raw_img *img = infile->sub_img + i;
		if (rast_to_raw_img(&desc.rast, img) != wu_ok
		|| !pnm_decode(&desc, img->data, i)) {
			break;
		}
		++i;
	}
	return image_file_total_decoded(infile, i);
}
