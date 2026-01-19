// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#ifndef LIB_PIKLIB
#define LIB_PIKLIB

#include "raster/wuimg.h"

struct piklib_desc {
	FILE *ifp;
	uint32_t mask_size;
};

struct wu_st piklib_load(struct piklib_desc *desc, struct wuimg *img);

struct wu_st piklib_init(struct piklib_desc *desc, struct wuimg *img,
FILE *ifp);

#endif /* LIB_PIKLIB */
