#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "../common.h"
#include "pal.h"

static uint8_t * expand_palette(const uint_fast8_t byte, uint8_t *restrict dst,
const struct raster_pal *cm, const size_t nr, const uint8_t ch,
const uint8_t bitdepth, const bool careful) {
	const int mask = (1 << bitdepth) - 1;
	size_t i = 8;
	const size_t bound = i - nr * bitdepth;
	while (i > bound) {
		i -= bitdepth;
		const int idx = (byte >> i) & mask;
		memcpy(dst, cm->color + idx, (careful) ? 3 : 4);
		dst += ch;
	}
	return dst;
}

static inline void palette_common(uint8_t *restrict dst,
const uint8_t *restrict src, const struct raster_pal *cm, size_t width,
const size_t height, const size_t alignment, const uint8_t ch,
const uint8_t bitdepth) {
	const size_t biab = 8 / bitdepth;

	const size_t bytes = width / biab;
	const size_t remainer = width % biab;
	const size_t scan = scanline_length(width, bitdepth, alignment);
	const size_t rowpad = scan - width / biab;

	const bool is_rgb = (ch == 3);
	for (size_t y = 0; y < height - is_rgb; ++y) {
		for (size_t x = 0; x < bytes; ++x) {
			dst = expand_palette(*src, dst, cm, biab, ch, bitdepth,
				false);
			++src;
		}
		dst = expand_palette(*src, dst, cm, remainer, ch, bitdepth, false);
		src += rowpad;
	}

	if (is_rgb) {
		for (size_t x = 0; x < bytes; ++x) {
			dst = expand_palette(*src, dst, cm, biab, ch, bitdepth,
				x == bytes - 1);
			++src;
		}
		dst = expand_palette(*src, dst, cm, remainer, ch, bitdepth, true);
	}
}

static void strip_palette_rgba8(uint8_t *restrict dst,
const uint8_t *restrict src, const struct raster_pal *cm, const size_t width,
const size_t height, const uint8_t alignment) {
	palette_common(dst, src, cm, width, height, alignment, 4, 8);
}

static void strip_palette_rgb8(uint8_t *restrict dst,
const uint8_t *restrict src, const struct raster_pal *cm, const size_t width,
const size_t height, const uint8_t alignment) {
	palette_common(dst, src, cm, width, height, alignment, 3, 8);
}

void raster_pal_expand(void *restrict dst, const uint8_t *restrict src,
const struct raster_pal *cm, const size_t width, const size_t height,
const uint8_t alignment, const uint8_t channels) {
	switch (channels) {
	case 3:
		strip_palette_rgb8(dst, src, cm, width, height, alignment);
		break;
	case 4:
		strip_palette_rgba8(dst, src, cm, width, height, alignment);
		break;
	}
}

void raster_pal_from_rgb8(struct raster_pal *pal, const void *restrict rgb,
const size_t nmemb) {
	pix_rgb8_to_rgba8(pal->color, rgb, nmemb);
}
