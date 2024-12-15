// SPDX-License-Identifier: 0BSD
#ifndef LIB_XYZ
#define LIB_XYZ

#include "misc/mparser.h"
#include "raster/wuimg.h"

size_t xyz_decode(const struct mparser *mp, struct wuimg *img);

enum wu_error xyz_parse(struct mparser *mp, struct wuimg *img);

enum wu_error xyz_init(struct mparser *mp, struct wuptr mem);

#endif /* LIB_XYZ */
