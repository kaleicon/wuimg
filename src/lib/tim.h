// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef LIB_TIM
#define LIB_TIM

#include <stdio.h>

#include "raster/wuimg.h"

struct tim_clut {
	uint16_t nb;
	uint16_t x, y;
	struct palette **clut;
};

struct tim_desc {
	FILE *ifp;
	void *raster;
	uint16_t x, y;
	struct tim_clut clut;
};

void tim_cleanup(struct tim_desc *desc);

void tim_alt_clut(struct tim_desc *desc, const struct wuimg *main,
struct wuimg *alt, const uint16_t clut_nb);

struct wu_st tim_decode_main(struct tim_desc *desc, struct wuimg *img);

struct wu_st tim_parse(struct tim_desc *desc, struct wuimg *img,
FILE *ifp);

#endif /* LIB_TIM */
