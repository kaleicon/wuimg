// SPDX-License-Identifier: 0BSD
#ifndef WU_IMGCONV
#define WU_IMGCONV

#include "raster/unpack.h"
#include "raster/wuimg.h"

struct imgconv {
	enum unpack_op op:8;
	uint8_t unpack_depth;
	uint8_t unpack_ch;
	struct color_convert color;
	cmsHTRANSFORM xfr;
	size_t row_len;
	uint8_t *row;
	struct raster_pal *pal;
	const struct wuimg *src;
	const struct wuimg *dst;
	struct wuimg *tmp;
};

void imgconv_close(struct imgconv *state);

uint8_t * imgconv_get_row(const struct imgconv *state, size_t y);

const char * imgconv_init(struct imgconv *state, const struct wuimg *dst,
const struct wuimg *src);

#endif /* WU_IMGCONV */
