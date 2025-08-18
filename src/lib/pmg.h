// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_PMG
#define LIB_PMG

#include "raster/wuimg.h"

struct wu_st pmg_decode(struct wuptr mem, struct wuimg *img);

struct wu_st pmg_init(struct wuptr mem, struct wuimg *img);

#endif /* LIB_PMG */
