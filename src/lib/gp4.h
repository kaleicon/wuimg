// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_GP4
#define LIB_GP4

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct gp4_desc {
	struct mparser mp;
	uint16_t x, y;
};

size_t gp4_decode(const struct gp4_desc *desc, struct wuimg *img);

enum wu_error gp4_parse(struct gp4_desc *desc, struct wuptr mem,
struct wuimg *img);

#endif /* LIB_GP4 */
