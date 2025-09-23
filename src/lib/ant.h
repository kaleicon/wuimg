// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_ANT
#define LIB_ANT

#include "raster/wuimg.h"

struct ant_desc {
	FILE *ifp;
	uint32_t size;
};

struct wu_st ant_decode(const struct ant_desc *desc, struct wuimg *img);

struct wu_st ant_init(struct ant_desc *desc, struct wuimg *img, FILE *ifp);

#endif /* LIB_ANT */
