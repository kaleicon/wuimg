#ifndef RAST_WUTILS
#define RAST_WUTILS

#include "wudefs.h"
#include "raster/lib.h"

bool rast_exceeds_size(const struct raster_desc *desc, const struct wu_conf *conf);

void rast_to_raw(struct raw_img *img, struct raster_desc *desc);

void rast_error(struct image_file *infile, enum lib_fail error);

#endif /* RAST_WUTILS */
