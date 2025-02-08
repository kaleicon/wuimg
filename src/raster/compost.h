// SPDX-License-Identifier: 0BSD
#ifndef ANIM_COMMON
#define ANIM_COMMON

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct compost {
	size_t x, y;
	size_t w, h;
};

void compost_alpha_blend(void *restrict dst, size_t w,
const void *restrict src, const struct compost *reg);

void compost_overwrite(void *restrict dst, size_t w, uint8_t ch,
const void *restrict src, const struct compost *reg);

void compost_clear(void *restrict dst, size_t w, uint8_t ch, int c,
const struct compost *reg);

bool compost_bounds_check(size_t w, size_t h, const struct compost *reg);

#endif /* ANIM_COMMON */
