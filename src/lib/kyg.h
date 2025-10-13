// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_KYG
#define LIB_KYG

#include "raster/wuimg.h"

struct kyg_desc {
	struct wuptr comment;
	struct wuptr data;
	uint16_t x, y;
};

struct wu_st kyg_decode(const struct kyg_desc *desc, struct wuimg *img);

struct wu_st kyg_parse(struct kyg_desc *desc, struct wuimg *img,
struct wuptr map);

#endif /* LIB_KYG */
