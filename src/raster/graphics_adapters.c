#include <string.h>

#include "common.h"

#include "strip.h"
#include "graphics_adapters.h"

static void interleave_pal1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t w, const size_t h, const uint8_t planes, const size_t row_len) {
	for (size_t y = 0; y < h; ++y) {
		const uint8_t *s = src + row_len*y;
		for (size_t x = 0; x < w; ++x) {
			int val = 0;
			for (uint8_t z = 0; z < planes; ++z) {
				const uint8_t byte = s[row_len*h*z + x/8];
				val |= (bool)(byte & (0x80 >> (x%8))) << z;
			}
			dst[x + w*y] = (uint8_t)val;
		}
	}
}

static void interleave_nopal1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t w, const size_t h, const uint8_t planes, const size_t row_len) {
	for (uint8_t y = 0; y < h; ++y) {
		const uint8_t *s = src + row_len*y;
		uint8_t *d = dst + w*planes*y;
		for (uint8_t z = 0; z < planes; ++z) {
			for (size_t x = 0; x < w; ++x) {
				const uint8_t byte = s[row_len*h*z + x/8];
				d[x + z] = (byte & (0x80 >> (x%8)))
					? 0xff : 0x00;
			}
		}
	}
}

static void interleave8(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t w, const uint8_t ch, const size_t row_len) {
	for (size_t z = 0; z < ch; ++z) {
		strip_spread(dst + z, src + row_len*z, w, ch);
	}
}

/* Interleaves bitplanes, each 'h' rows in size. For row-interleaving,
 * must be called with h = 1 on a loop. */
void vga_interleave(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t w, const size_t h, const uint8_t ch, const uint8_t bitdepth,
const align_t align, const bool paletted) {
	const size_t row_len = scanline_length(w, bitdepth, align);
	switch (bitdepth) {
	case 1:
		if (paletted) {
			interleave_pal1(dst, src, w, h, ch, row_len);
		} else {
			interleave_nopal1(dst, src, w, h, ch, row_len);
		}
		break;
	case 8:
		// Only used by PCX.
		interleave8(dst, src, w, ch, row_len);
		break;
	}
}

static int unpack_ykj_chroma(const uint8_t *src) {
	const unsigned n = (src[0] & 0x07) | ((src[1] & 0x07) << 3);
	return (int)((n ^ 0x20) - 0x20);
}

void v9958_ykj_to_grb(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t dwords, const struct raster_pal *yae) {
	/*
		G = Y + K
		R = Y + J
		B = 5*Y/4 - J/2 - K/4
	*/
	for (size_t i = 0; i < dwords; ++i) {
		const int k = unpack_ykj_chroma(src + i*4);
		const int j = unpack_ykj_chroma(src + i*4 + 2);
		for (size_t p = 0; p < 4; ++p) {
			const size_t pos = i*4 + p;
			const int y = src[pos] >> 3;
			if (yae && (y & 1)) {
				memcpy(dst + pos*3, yae->color + y/2,
					(i + 1 == dwords) ? 3 : 4);
			} else {
				const int g = iclamp(y + k, 0, 0x1f);
				const int r = iclamp(y + j, 0, 0x1f);
				const int b = iclamp(y*5/4 - j/2 - k/4, 0, 0x1f);
				dst[pos*3] = (uint8_t)g;
				dst[pos*3+1] = (uint8_t)r;
				dst[pos*3+2] = (uint8_t)b;
			}
		}
	}
}

// Can't be bothered to write tables
struct pix_rgba8 ega_palette(const size_t idx) {
	const size_t r = ((idx >> 1) & 2) | ((idx >> 5) & 1);
	const size_t g = ((idx     ) & 2) | ((idx >> 4) & 1);
	const size_t b = ((idx << 1) & 2) | ((idx >> 3) & 1);
	return (struct pix_rgba8) {
		.r = (uint8_t)(r * 0x55),
		.g = (uint8_t)(g * 0x55),
		.b = (uint8_t)(b * 0x55),
		.a = 0xff,
	};
}

struct pix_rgba8 cga_palette(const size_t idx) {
	const bool brown_circuit = (idx == 6);
	const size_t bright = idx >> 3;
	const size_t r = ((idx >> 1) & 2) | bright;
	const size_t g = ((idx       & 2) | bright) - brown_circuit;
	const size_t b = ((idx << 1) & 2) | bright;
	return (struct pix_rgba8) {
		.r = (uint8_t)(r * 0x55),
		.g = (uint8_t)(g * 0x55),
		.b = (uint8_t)(b * 0x55),
		.a = 0xff,
	};
}
