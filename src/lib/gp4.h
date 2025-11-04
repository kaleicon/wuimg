// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_GP4
#define LIB_GP4

#include "raster/wuimg.h"

struct gp4_desc {
	struct wuptr data;
	uint16_t x, y;
};

struct wu_st gp4_decode(const struct gp4_desc *desc, struct wuimg *img);

struct wu_st gp4_parse(struct gp4_desc *desc, struct wuptr mem,
struct wuimg *img);

#endif /* LIB_GP4 */
