// SPDX-License-Identifier: 0BSD
#ifndef LIB_GP4
#define LIB_GP4

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct gp4_desc {
	struct mparser mp;
	uint16_t x, y;
};

size_t gp4_decode(const struct gp4_desc *desc, struct wuimg *img);

enum wu_error gp4_parse(struct gp4_desc *desc, const struct map_info map,
struct wuimg *img);

#endif /* LIB_GP4 */
