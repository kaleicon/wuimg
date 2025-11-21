// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_TIM2
#define LIB_TIM2

#include <stdio.h>

#include "raster/wuimg.h"

struct tim2_desc {
	FILE *ifp;
	long next_off;
	long pal_off;
	uint8_t version;
	align_t align;
	uint16_t nr;
	bool pal_compound;
	uint8_t pal_depth;
	uint8_t mipmaps;
	uint32_t idx;
};

struct wu_st tim2_load(struct tim2_desc *desc, struct wuimg *img);

struct wu_st tim2_next(struct tim2_desc *desc, struct wuimg *img);

struct wu_st tim2_init(struct tim2_desc *desc, FILE *ifp);

#endif /* LIB_TIM2 */
