// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef ANIM_COMMON
#define ANIM_COMMON

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "raster/pal.h"

struct wuimg; // forward decl

struct compost {
	size_t x, y;
	size_t w, h;
};

void compost_alpha_blend(const struct compost *reg, const struct wuimg *img,
const uint8_t *restrict src);

// Expand palette, treating `alpha_idx` as fully transparent if -1.
void compost_pal_expand_idx_ignore(const struct compost *reg,
const struct wuimg *img, const uint8_t *restrict src, int alpha_idx,
const struct palette *pal);

// Overwrites `img` region with `src`, to which opaque alpha is added
void compost_overwrite_add_alpha(const struct compost *reg,
const struct wuimg *img, const uint8_t *restrict src);

// Overwrites `img` region with `src`
void compost_overwrite(const struct compost *reg, const struct wuimg *img,
const uint8_t *restrict src);

// Clears `img` region with zeros
void compost_clear(const struct compost *reg, const struct wuimg *img);

// Copies `img` region to dst
void compost_extract(const struct compost *reg, uint8_t *restrict dst,
const struct wuimg *img);

// Expands `aa` to cover `bb`
void compost_affect(struct compost *restrict aa,
const struct compost *restrict bb);

bool compost_is_full(const struct compost *reg, const struct wuimg *img);

bool compost_bounds_check(const struct compost *reg, const struct wuimg *img);

#endif /* ANIM_COMMON */
