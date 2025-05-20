// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_JAM
#define LIB_JAM

#include "misc/mparser.h"
#include "raster/wuimg.h"

size_t jam_decode(struct mparser mp, struct wuimg *img);

enum wu_error jam_parse(struct mparser *mp, struct wuimg *img);

enum wu_error jam_identify(struct mparser *mp, struct wuptr map);

#endif /* LIB_JAM */
