#ifndef LIB_QOI
#define LIB_QOI

#include "wudefs.h"
#include "raster/memparser.h"

size_t qoi_decode(struct mp_parser *mp, struct raw_img *img);

enum wu_error qoi_parse(struct mp_parser *mp, struct raw_img *img);

enum wu_error qoi_open(struct mp_parser *mp, const struct map_info *mm);

#endif /* LIB_QOI */
