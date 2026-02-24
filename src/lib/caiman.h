// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#ifndef LIB_CAIMAN
#define LIB_CAIMAN

#include "raster/wuimg.h"

struct caiman_desc {
	FILE *ifp;
	long off;
	uint16_t nr_images;
};

struct wu_st caiman_img_info(struct caiman_desc *desc, struct wuimg *img,
uint16_t idx);

struct wu_st caiman_init(struct caiman_desc *desc, FILE *ifp);

#endif /* LIB_CAIMAN */
