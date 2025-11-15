// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef LIB_XYZ
#define LIB_XYZ

#include "raster/wuimg.h"

struct wu_st xyz_decode(struct wuimg *img, struct wuptr mem);

struct wu_st xyz_parse(struct wuimg *img, struct wuptr mem);

#endif /* LIB_XYZ */
