// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_WGTSPR
#define LIB_WGTSPR

#include "raster/wuimg.h"

struct wgtspr_desc {
	FILE *ifp;
	uint16_t version;
	uint32_t sprites;
	struct palette *pal;
};

void wgtspr_cleanup(struct wgtspr_desc *desc);

struct wu_st wgtspr_get_sprite(const struct wgtspr_desc *desc, struct wuimg *img);

struct wu_st wgtspr_next_sprite(struct wgtspr_desc *desc, struct wuimg *img);

struct wu_st wgtspr_init(struct wgtspr_desc *desc, FILE *ifp);

#endif /* LIB_WGTSPR */
