#include <stdlib.h>

#include "rast_utils.h"
#include "common.h"
#include "raster/lib.h"

void rast_to_raw(struct raw_img *img, struct raster_desc *desc) {
	if (desc->palette) {
		raw_img_set_palette(img, lib_raster_take_palette(desc));
	}
	img->w = desc->w;
	img->h = desc->h;
	img->channels = desc->ch;
	img->bitdepth = desc->bitdepth;
	img->alignment = desc->alignment;
	img->layout = desc->layout;
	img->attr = desc->attr;
	if (desc->planar && raw_img_plane_init(img)) {
		raw_img_plane_resolve(img);
	}
}

size_t rast_to_raw_img(struct raster_desc *desc, struct raw_img *img) {
	const size_t size = raster_size(desc);
	img->data = malloc(size);
	if (img->data) {
		rast_to_raw(img, desc);
		return size;
	}
	return 0;
}

bool rast_exceeds_size(const struct raster_desc *desc,
const struct wu_conf *conf) {
	return zumax(desc->w, desc->h) > conf->max_img_size;
}

void rast_error(struct image_file *infile, const enum lib_fail error) {
	image_file_error_append(infile, lib_fail_string(error));
}

enum wu_error rast_trivial_dec(struct image_file *infile,
const struct wu_conf *wuconf, rast_open_t open_fn) {
	struct raster_desc desc;
	const enum lib_fail status = (*open_fn)(&desc, infile->ifp);
	if (status != lib_ok) {
		rast_error(infile, status);
		return wu_unexpected_eof;
	}

	if (rast_exceeds_size(&desc, wuconf)) {
		return wu_exceeds_size_limit;
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (img) {
		struct memory mem;
		if (lib_load_rast(&mem, &desc, infile->ifp)) {
			img->data = mem.data;
			rast_to_raw(img, &desc);
			return wu_ok;
		}
	}
	return wu_alloc_error;
}
