#ifndef RAST_WUTILS
#define RAST_WUTILS

#include "wudefs.h"
#include "raster/raster.h"

typedef enum wu_error (*rast_open_t)(struct raw_img *img, FILE *ifp);

typedef enum wu_error (*rast_map_t)(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm);


bool rast_exceeds_size(const struct raster_desc *desc, const struct wu_conf *conf);

enum wu_error rast_to_raw_img(struct raster_desc *desc, struct raw_img *img);

enum wu_error rast_map_wrap(struct image_file *infile,
const struct wu_conf *wuconf, rast_map_t wrap_fn);

enum wu_error rast_fread_dec(struct image_file *infile,
const struct wu_conf *wuconf, rast_open_t open_fn);

#endif /* RAST_WUTILS */
