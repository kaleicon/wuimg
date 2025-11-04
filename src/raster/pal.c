// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/mem.h"
#include "raster/pal.h"

void palette_unref(struct palette *cm) {
	if (cm && cm->refs) {
		--cm->refs;
	} else {
		free(cm);
	}
}

struct palette * palette_ref(struct palette *cm) {
	++cm->refs;
	return cm;
}

struct palette * palette_copy(struct palette *cm) {
	struct palette *copy = memdup(cm, sizeof(*cm));
	if (copy) {
		copy->refs = 0;
	}
	return copy;
}

struct palette * palette_new(void) {
	return calloc(1, sizeof(struct palette));
}

static uint8_t * expand_palette(const uint_fast8_t byte, uint8_t *restrict dst,
const struct palette *cm, const size_t items, const uint8_t bitdepth) {
	const uint8_t ch = 4;
	const int mask = (1 << bitdepth) - 1;
	size_t i = 8;
	const size_t lower_bound = i - items * bitdepth;
	while (i > lower_bound) {
		i -= bitdepth;
		const int idx = (byte >> i) & mask;
		memcpy(dst, cm->color + idx, ch);
		dst += ch;
	}
	return dst;
}

static inline void palette_common(uint8_t *restrict dst,
const uint8_t *restrict src, const struct palette *cm, size_t width,
const uint8_t bitdepth) {
	const size_t biab = 8 / bitdepth;

	const size_t bytes = width / biab;
	const size_t remainer = width % biab;
	for (size_t x = 0; x < bytes; ++x) {
		dst = expand_palette(*src, dst, cm, biab, bitdepth);
		++src;
	}
	expand_palette(*src, dst, cm, remainer, bitdepth);
}

static void strip_palette_rgba8(uint8_t *restrict dst,
const uint8_t *restrict src, const struct palette *cm, const size_t width) {
	palette_common(dst, src, cm, width, 8);
}

static void strip_palette_rgba4(uint8_t *restrict dst,
const uint8_t *restrict src, const struct palette *cm, const size_t width) {
	palette_common(dst, src, cm, width, 4);
}

static void strip_palette_rgba2(uint8_t *restrict dst,
const uint8_t *restrict src, const struct palette *cm, const size_t width) {
	palette_common(dst, src, cm, width, 2);
}

static void strip_palette_rgba1(uint8_t *restrict dst,
const uint8_t *restrict src, const struct palette *cm, const size_t width) {
	palette_common(dst, src, cm, width, 1);
}

void palette_expand(void *restrict dst, const uint8_t *restrict src,
const struct palette *cm, const size_t width, const uint8_t bitdepth) {
	switch (bitdepth) {
	case 1: strip_palette_rgba1(dst, src, cm, width); break;
	case 2: strip_palette_rgba2(dst, src, cm, width); break;
	case 4: strip_palette_rgba4(dst, src, cm, width); break;
	case 8: strip_palette_rgba8(dst, src, cm, width); break;
	}
}

void palette_from_rgb8_bitrange(struct palette *dst, const void *src,
const size_t nmemb, const uint8_t bitrange) {
	const uint8_t max = (uint8_t)bit_set32(bitrange);
	const struct pix_rgb8 *s = src;
	for (size_t i = 0; i < nmemb; ++i) {
		memmove(dst->color + i, s + i, (i + 1 < nmemb) ? 4 : 3);
		dst->color[i].a = max;
	}
}

void palette_from_rgb8(struct palette *dst, const void *src,
const size_t nmemb) {
	palette_from_rgb8_bitrange(dst, src, nmemb, 8);
}

bool palette_from_file(struct palette *dst, const uint8_t size,
const size_t nmemb, FILE *ifp, const uint8_t bitrange) {
	uint8_t *src = (uint8_t *)dst->color + (4 - size) * nmemb;
	if (fread(src, size*nmemb, 1, ifp)) {
		if (size == 3) {
			palette_from_rgb8_bitrange(dst, src, nmemb, bitrange);
		}
		return true;
	}
	return false;
}


void palette_cycle_render(struct palette *dst, const struct palette_cycle *src,
const double time) {
	for (uint16_t c = 0; c < src->len; ++c) {
		const struct palette_crng *crng = src->crng + c;
		if (crng->active) {
			const uint8_t lo = crng->lo;
			const size_t cnt = crng->hi + 1u - lo;
			size_t i = (size_t)(time/crng->secs) % cnt;
			if (crng->reverse) {
				i = cnt - i;
			}

			memcpy(dst->color + lo + i, src->color + lo, (cnt - i)*4);
			memcpy(dst->color + lo, src->color + lo + cnt - i, i*4);
		}
	}
}

void palette_cycle_set(struct palette_cycle *dst, const struct palette *src) {
	memcpy(dst->color, src->color, sizeof(src->color));
}

struct palette_cycle * palette_cycle_new(const uint8_t slots) {
	struct palette_cycle *cycle = calloc(
		sizeof(*cycle) + slots * sizeof(*cycle->crng), 1
	);
	if (cycle) {
		cycle->alloc = slots;
	}
	return cycle;
}
