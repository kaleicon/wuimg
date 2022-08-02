#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "common.h"
#include "raster/pal.h"

void raster_pal_print(const struct raster_pal *cm) {
	for (size_t i = 0; i < ARRAY_LEN(cm->color); ++i) {
		const struct pix_rgba8 *pix = cm->color + i;
		fprintf(stderr, "%zu: %hhx, %hhx, %hhx, %hhx\n",
			i, pix->r, pix->g, pix->b, pix->a);
	}
}

static uint8_t * expand_palette(const uint_fast8_t byte, uint8_t *restrict dst,
const struct raster_pal *cm, const size_t items, const uint8_t bitdepth) {
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
const uint8_t *restrict src, const struct raster_pal *cm, size_t width,
const size_t height, const uint8_t alignment, const uint8_t bitdepth) {
	const size_t biab = 8 / bitdepth;

	const size_t bytes = width / biab;
	const size_t remainer = width % biab;
	const size_t scan = scanline_length(width, bitdepth, alignment);
	const size_t rowpad = scan - bytes;

	for (size_t y = 0; y < height; ++y) {
		for (size_t x = 0; x < bytes; ++x) {
			dst = expand_palette(*src, dst, cm, biab, bitdepth);
			++src;
		}
		dst = expand_palette(*src, dst, cm, remainer, bitdepth);
		src += rowpad;
	}
}

static void strip_palette_rgba8(uint8_t *restrict dst,
const uint8_t *restrict src, const struct raster_pal *cm, const size_t width,
const size_t height, const uint8_t alignment) {
	palette_common(dst, src, cm, width, height, alignment, 8);
}

static void strip_palette_rgba4(uint8_t *restrict dst,
const uint8_t *restrict src, const struct raster_pal *cm, const size_t width,
const size_t height, const uint8_t alignment) {
	palette_common(dst, src, cm, width, height, alignment, 4);
}

static void strip_palette_rgba2(uint8_t *restrict dst,
const uint8_t *restrict src, const struct raster_pal *cm, const size_t width,
const size_t height, const uint8_t alignment) {
	palette_common(dst, src, cm, width, height, alignment, 2);
}

static void strip_palette_rgba1(uint8_t *restrict dst,
const uint8_t *restrict src, const struct raster_pal *cm, const size_t width,
const size_t height, const uint8_t alignment) {
	palette_common(dst, src, cm, width, height, alignment, 1);
}

void raster_pal_expand(void *restrict dst, const uint8_t *restrict src,
const struct raster_pal *cm, const size_t width, const size_t height,
const uint8_t alignment, const uint8_t bitdepth) {
	switch (bitdepth) {
	case 1: strip_palette_rgba1(dst, src, cm, width, height, alignment); break;
	case 2: strip_palette_rgba2(dst, src, cm, width, height, alignment); break;
	case 4: strip_palette_rgba4(dst, src, cm, width, height, alignment); break;
	case 8: strip_palette_rgba8(dst, src, cm, width, height, alignment); break;
	}
}

void raster_pal_from_rgb8(struct raster_pal *dst, const void *src,
const size_t nmemb) {
	const struct pix_rgb8 *s = src;
	for (size_t i = 0; i < nmemb; ++i) {
		memcpy(dst->color + i, s + i, (i + 1 < nmemb) ? 4 : 3);
		dst->color[i].a = 0xff;
	}
}
