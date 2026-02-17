// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#ifndef LIB_PIFF
#define LIB_PIFF

#include "raster/wuimg.h"

struct wu_st vvtp_next(FILE *ifp, struct wuimg *img);

struct wu_st vvtp_init(FILE *ifp);

#endif /* LIB_PIFF */
