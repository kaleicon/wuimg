// SPDX-License-Identifier: 0BSD
#ifndef LIB_WGTSPR
#define LIB_WGTSPR

#include "raster/wuimg.h"

struct wgtspr_desc {
	FILE *ifp;
	uint16_t version;
	uint32_t sprites;
	struct raster_pal pal;
};

size_t wgtspr_get_sprite(const struct wgtspr_desc *desc, struct wuimg *img);

enum wu_error wgtspr_next_sprite(struct wgtspr_desc *desc, struct wuimg *img);

enum wu_error wgtspr_init(struct wgtspr_desc *desc, FILE *ifp);

#endif /* LIB_WGTSPR */
