// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_PXAN
#define LIB_PXAN

#include "misc/mparser.h"
#include "raster/wuimg.h"

enum pim_type {
	pim_static = 1,
	pim_anim = 3,
};

struct pim_desc {
	struct mparser mp;
	struct wuimg *img;
	uint16_t frames;
	enum pim_type type:8;
	size_t start;
	const uint8_t *frame_sizes;
};

struct wu_st pim_decode(struct pim_desc *desc, struct wuimg *img);

struct wu_st pim_parse(struct pim_desc *desc, struct wuimg *img, struct wuptr mem);

#endif /* LIB_PXAN */
