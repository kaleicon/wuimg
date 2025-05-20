// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef LIB_TIM
#define LIB_TIM

#include <stdio.h>

#include "raster/wuimg.h"

struct tim_clut {
	uint16_t nb;
	uint16_t x, y;
};

struct tim_desc {
	FILE *ifp;
	uint16_t x, y;
	struct tim_clut clut;
};

size_t tim_decode(const struct tim_desc *desc, struct wuimg *img);

enum wu_error tim_parse_header(struct tim_desc *desc, struct wuimg *img);

enum wu_error tim_open_file(struct tim_desc *desc, FILE *ifp);

#endif /* LIB_TIM */
