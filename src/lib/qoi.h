// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef LIB_QOI
#define LIB_QOI

#include "raster/wuimg.h"
#include "misc/mparser.h"

struct wu_st qoi_decode(const struct mparser *mp, struct wuimg *img);

struct wu_st qoi_parse(struct mparser *mp, struct wuimg *img, struct wuptr mem);

#endif /* LIB_QOI */
