// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#ifndef LIB_ERI
#define LIB_ERI

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct eri_desc {
	struct mparser mp;
	struct wuimg *img;
	uint8_t blocking_degree;
};

struct wu_st eri_decode(struct eri_desc *desc);

struct wu_st eri_init(struct eri_desc *desc, struct wuimg *img,
struct wuptr mem);

#endif /* LIB_ERI */
