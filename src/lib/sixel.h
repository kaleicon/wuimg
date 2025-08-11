// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#ifndef LIB_SIXEL
#define LIB_SIXEL
#include "raster/wuimg.h"
#include "misc/mparser.h"

enum sixel_background_color {
	sixel_set_to_bg = 0,
	sixel_retain = 1,
};

struct sixel_desc {
	struct mparser tp;

	enum sixel_background_color p2;
	unsigned horizontal_grid_size;
};

struct wu_st sixel_decode(const struct sixel_desc *desc, struct wuimg *img);

struct wu_st sixel_try_parse(struct sixel_desc *desc, struct wuimg *img,
struct wuptr mem, size_t dcs_search_limit);

#endif // LIB_SIXEL
