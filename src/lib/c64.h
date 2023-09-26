// SPDX-License-Identifier: 0BSD
#ifndef LIB_C64
#define LIB_C64

#include "misc/mparser.h"
#include "raster/wuimg.h"

struct c64_mem_offsets {
	const uint8_t *restrict bitmap;
	const uint8_t *restrict screen;
	const uint8_t *restrict color;
	const uint8_t *restrict bg;
};

bool c64_decode(const struct mparser *mp, struct wuimg *img);

enum wu_error c64_guess(const struct mparser *mp, struct wuimg *img);

#endif /* LIB_C64 */
