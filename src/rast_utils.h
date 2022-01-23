#ifndef RAST_WUTILS
#define RAST_WUTILS

#include "wudefs.h"
#include "raster/lib.h"

typedef enum lib_fail (*rast_open_t)(struct raster_desc *desc, FILE *ifp);


void rast_to_raw(struct raw_img *img, struct raster_desc *desc);

size_t rast_to_raw_img(struct raster_desc *desc, struct raw_img *img);

bool rast_exceeds_size(const struct raster_desc *desc, const struct wu_conf *conf);

void rast_error(struct image_file *infile, enum lib_fail error);

enum wu_error rast_trivial_dec(struct image_file *infile,
const struct wu_conf *conf, rast_open_t open_fn);

#endif /* RAST_WUTILS */
