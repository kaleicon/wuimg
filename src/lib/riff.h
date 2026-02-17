// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#ifndef LIB_RIFFPAL
#define LIB_RIFFPAL

#include "raster/wuimg.h"

struct riffpal_desc {
	FILE *ifp;
	uint16_t entries;
};

struct wu_st riffpal_load(struct riffpal_desc *desc, struct wuimg *img);

struct wu_st riffpal_init(struct riffpal_desc *desc, struct wuimg *img,
FILE *ifp);

#endif /* LIB_RIFFPAL */
