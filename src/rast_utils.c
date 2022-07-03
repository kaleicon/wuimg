#include <stdlib.h>

#include "rast_utils.h"
#include "common.h"

bool rast_exceeds_size(const struct raster_desc *desc,
const struct wu_conf *conf) {
	return zumax(desc->w, desc->h) > conf->max_img_size;
}

enum wu_error rast_to_raw_img(struct raster_desc *desc, struct raw_img *img) {
	img->w = desc->w;
	img->h = desc->h;
	img->channels = desc->ch;
	img->bitdepth = desc->bitdepth;
	img->alignment = desc->alignment;
	img->layout = desc->layout;
	img->attr = desc->attr;
	img->rotate = desc->rotate;
	img->mirror = desc->mirror;
	return raw_img_alloc(img);
}

enum wu_error rast_map_wrap(struct image_file *infile,
const struct wu_conf *wuconf, rast_map_t wrap_fn) {
	struct map_info mm;
	enum wu_error err = wu_open_error;
	if (map_file(&mm, infile->ifp)) {
		err = (*wrap_fn)(infile, wuconf, &mm);
		unmap_file(&mm);
	}
	return err;
}

enum wu_error rast_fread_dec(struct image_file *infile,
const struct wu_conf *wuconf, rast_open_t open_fn) {
	struct raw_img *img = alloc_sub_images(infile, 1);
	if (img) {
		enum wu_error st = (*open_fn)(img, infile->ifp);
		if (st == wu_ok) {
			if (!raw_img_exceeds_limit(img, wuconf)) {
				st = raw_img_alloc(img);
				if (st == wu_ok) {
					return fread(img->data, 1,
						raw_img_size(img), infile->ifp)
						? wu_ok : wu_unexpected_eof;
				}
				return st;
			}
			return wu_exceeds_size_limit;
		}
		return st;
	}
	return wu_alloc_error;
}
