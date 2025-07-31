// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_NOKIA
#define LIB_NOKIA

#include "raster/wuimg.h"

struct nol_desc {
	FILE *ifp;
	bool is_nol;
	uint16_t country, network;
	uint16_t mystery;
};

struct wu_st nol_load(struct nol_desc *desc, struct wuimg *img);

struct wu_st nol_parse(struct nol_desc *desc, struct wuimg *img, FILE *ifp);

#endif /* LIB_NOKIA */
