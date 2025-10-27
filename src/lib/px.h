// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef LIB_PX
#define LIB_PX

#include "misc/mparser.h"
#include "raster/wuimg.h"

enum px_type {
	px_type_01 = 0x01,
	px_type_04 = 0x04,
	px_type_07 = 0x07,
	px_type_0c = 0x0c,
	px_type_90 = 0x90,
	px_type_40 = 0x40,
	px_type_44 = 0x44,
};

struct px_tile {
	uint32_t size;
	uint16_t x, y;
	size_t data_start;
};

struct px_desc {
	struct mparser mp;
	uint32_t nr;
	uint16_t w, h;
	enum px_type type:16;
	struct px_tile tile;
};

struct wu_st px_get_image(const struct px_desc *desc, struct wuimg *img,
uint32_t idx);

struct wu_st px_set_info(const struct px_desc *desc, struct wuimg *img);

struct wu_st px_parse(struct px_desc *desc, struct wuptr mem);

#endif /* LIB_PX */
