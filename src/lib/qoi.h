// SPDX-License-Identifier: 0BSD
#ifndef LIB_QOI
#define LIB_QOI

#include "raster/wuimg.h"
#include "misc/mparser.h"

size_t qoi_decode(const struct mparser *mp, struct wuimg *img);

enum wu_error qoi_parse(struct mparser *mp, struct wuimg *img);

enum wu_error qoi_open(struct mparser *mp);

#endif /* LIB_QOI */
