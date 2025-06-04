// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef RASTER_PAL
#define RASTER_PAL

#include "pix.h"

struct palette {
	uint32_t refs;
	struct pix_rgba8 color[256];
};

void palette_print(const struct palette *cm);

void palette_unref(struct palette *pal);

struct palette * palette_ref(struct palette *cm);

struct palette * palette_copy(struct palette *cm);

struct palette * palette_new(void);

void palette_expand(void *restrict dst, const uint8_t *restrict src,
const struct palette *cm, size_t width, uint8_t bitdepth);

void palette_from_rgb8_bitrange(struct palette *dst, const void *src,
size_t nmemb, uint8_t bitrange);

void palette_from_rgb8(struct palette *dst, const void *src, size_t nmemb);

bool palette_from_file(struct palette *dst, uint8_t size, size_t nmemb,
FILE *src, uint8_t bitrange);


struct palette_crng {
	uint8_t lo;
	uint8_t hi;
	bool reverse;
	bool active;
	float secs;
};

struct palette_cycle {
	uint8_t alloc;
	uint8_t len;
	uint8_t active_nr;
	bool too_many;
	struct pix_rgba8 color[256];
	struct palette_crng crng[];
};

void palette_cycle_render(struct palette *dst, const struct palette_cycle *src,
double time);

void palette_cycle_set(struct palette_cycle *dst, const struct palette *src);

struct palette_cycle * palette_cycle_new(uint8_t slots);

#endif /* RASTER_PAL */
