// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_JAM
#define LIB_JAM

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct wu_st jam_decode(struct mparser mp, struct wuimg *img);

struct wu_st jam_parse(struct mparser *mp, struct wuimg *img);

#endif /* LIB_JAM */
