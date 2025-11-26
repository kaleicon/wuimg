// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef WU_IMGCONV
#define WU_IMGCONV

#include "raster/unpack.h"
#include "raster/wuimg.h"

struct imgconv {
	// swizzle map
	uint8_t swz[4];

	// unpack_strip() parameters
	enum unpack_op op:8;
	uint8_t unpack_depth;
	uint8_t unpack_ch;

	struct color_convert color;
	struct icc_transform *xfr;
	size_t row_len;
	size_t unpack_len;
	uint8_t *row;
	struct palette *pal;
	const struct wuimg *src;
	const struct wuimg *dst;
	struct wuimg *tmp;
};

void imgconv_close(struct imgconv *state);

uint8_t * imgconv_get_row(const struct imgconv *state, size_t y,
void *restrict tgt);

const char * imgconv_init(struct imgconv *state, const struct wuimg *dst,
const struct wuimg *src);

#endif /* WU_IMGCONV */
