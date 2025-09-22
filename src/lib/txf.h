// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_TXF
#define LIB_TXF

#include "raster/wuimg.h"

struct txf_desc {
	FILE *ifp;
	uint32_t max_ascent, max_descent;
};

struct wu_st txf_load(const struct txf_desc *desc, struct wuimg *img);

struct wu_st txf_parse(struct txf_desc *desc, struct wuimg *img, FILE *ifp);

#endif /* LIB_TXF */
