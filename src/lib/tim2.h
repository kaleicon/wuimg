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
	uint32_t idx;
	uint8_t version;
	align_t align;
	uint16_t nr;

	uint32_t texa_fba_pabe;

	uint16_t pal_elems;
	uint8_t pal_depth;
	bool pal_compound;
	uint8_t mipmaps;
};

struct wu_st tim2_load(struct tim2_desc *desc, struct wuimg *img);

struct wu_st tim2_next(struct tim2_desc *desc, struct wuimg *img);

struct wu_st tim2_init(struct tim2_desc *desc, FILE *ifp);

#endif /* LIB_TIM2 */
