// SPDX-License-Identifier: 0BSD
#ifndef LIB_GPC
#define LIB_GPC

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct gpc_img_settings {
	uint32_t row_skip;
	uint32_t comp_len;
	uint16_t x, y;
};

struct gpc_desc {
	struct mparser mp;
	uint32_t main_row_skip;
	uint32_t img_off, sub_off;
	uint32_t nb;
	const uint8_t *sub_info;
	struct wuptr maker;
	struct palette *pal;
	struct gpc_img_settings cur;
};

void gpc_cleanup(struct gpc_desc *desc);

size_t gpc_decode(const struct gpc_desc *desc, struct wuimg *img);

enum wu_error gpc_set_image(struct gpc_desc *desc, struct wuimg *img,
uint32_t i);

enum wu_error gpc_parse(struct gpc_desc *desc);

enum wu_error gpc_init(struct gpc_desc *desc, struct wuptr mem);


size_t clm_load(FILE *ifp, struct wuimg *img);

enum wu_error clm_parse(FILE *ifp, struct wuimg *img);

#endif /* LIB_GPC */
