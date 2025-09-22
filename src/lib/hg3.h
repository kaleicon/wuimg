// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef LIB_HG3
#define LIB_HG3

#include "raster/wuimg.h"
#include "misc/mparser.h"

struct hg3_desc {
	struct mparser mp;
	struct mparser image;
	int32_t x, y;
	uint32_t canvas_w, canvas_h;
};

struct wu_st hg3_decode(const struct hg3_desc *desc, struct wuimg *img);

struct wu_st hg3_parse_image(struct hg3_desc *desc, struct wuimg *img);

struct wu_st hg3_next_image(struct hg3_desc *desc);

struct wu_st hg3_open(struct hg3_desc *desc, struct wuptr mem);

#endif /* LIB_HG3 */
