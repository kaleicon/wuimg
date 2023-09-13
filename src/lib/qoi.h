// SPDX-License-Identifier: 0BSD
#ifndef LIB_QOI
#define LIB_QOI

#include "raster/wuimg.h"
#include "misc/memparser.h"

size_t qoi_decode(const struct mp_parser *mp, struct wuimg *img);

enum wu_error qoi_parse(struct mp_parser *mp, struct wuimg *img);

enum wu_error qoi_open(struct mp_parser *mp);

#endif /* LIB_QOI */
