// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef RASTER_STRIP
#define RASTER_STRIP

#include <stdbool.h>
#include <stdint.h>

#include "raster/pal.h"

struct sewing_clothe {
	uint8_t *ptr;
	size_t stride;
	size_t len;
};

struct sewing_machine {
	uint8_t out_ch;
	uint8_t ch;
	bool compact;
	const struct palette *pal;
	size_t w, h;
	struct sewing_clothe dst, color, alpha;
};

typedef int8_t align_t;

align_t align_from_int(size_t alignment);

size_t strip_base(size_t width, uint8_t bitdepth);

size_t strip_length(size_t width, uint8_t bitdepth, align_t alignment);

size_t strip_padding(size_t width, uint8_t bitdepth, align_t alignment);

align_t strip_alignment(size_t stride, size_t width, uint8_t bitdepth);


/* Copies 'width' bytes from 'src' to 'dst', with 'ch' bytes of spacing */
void strip_spread(uint8_t *restrict dst, const uint8_t *restrict src,
size_t width, size_t ch);


void strip_handsew_alpha(void *dst, const void *color,
const void *alpha, const size_t w, const struct palette *pal,
const uint8_t ch);

void strip_sew_alpha(struct sewing_machine *sew);

void strip_sew_free_alpha(struct sewing_machine *sew);

bool strip_sew_alloc_alpha(struct sewing_machine *sew);

void strip_sew_init(struct sewing_machine *sew, void *restrict dst,
const struct palette *pal, size_t w, size_t h, uint8_t ch, align_t align,
bool will_sew);

#endif // RASTER_STRIP
