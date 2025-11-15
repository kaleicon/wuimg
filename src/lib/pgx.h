// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#ifndef LIB_PGX
#define LIB_PGX

#include "raster/wuimg.h"

struct wu_st pgx_decode(const struct wuptr comp, struct wuimg *img);

struct wu_st pgx_read_header(struct wuptr *comp, struct wuptr mem,
struct wuimg *img);

#endif /* LIB_PGX */
