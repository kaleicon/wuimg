// SPDX-License-Identifier: 0BSD
#ifndef LIB_C64
#define LIB_C64

#include "misc/mparser.h"
#include "raster/wuimg.h"

bool c64_decode(const struct mparser *mp, struct wuimg *img);

enum wu_error c64_guess(const struct mparser *mp, struct wuimg *img);

#endif /* LIB_C64 */
