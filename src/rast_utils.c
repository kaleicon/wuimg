#include "rast_utils.h"
#include "common.h"
#include "raster/lib.h"

bool rast_exceeds_size(const struct raster_desc *desc, const struct wu_conf *conf) {
	return zumax(desc->w, desc->h) > conf->max_img_size;
}

void rast_to_raw(struct raw_img *img, struct raster_desc *desc) {
	img->palette = lib_raster_take_palette(desc);
	img->w = desc->w;
	img->h = desc->h;
	img->channels = desc->ch;
	img->bitdepth = desc->bitdepth;
	img->alignment = desc->alignment;
	img->layout = desc->layout;
	img->attr = desc->attr;
}
