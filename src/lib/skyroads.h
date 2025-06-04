// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_SKYROADS
#define LIB_SKYROADS

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct wu_st skyroads_decode(struct mparser mp, struct wuimg *img);

struct wu_st skyroads_parse(struct mparser *mp, struct wuimg *img,
struct wuptr mem);

#endif // LIB_SKYROADS
