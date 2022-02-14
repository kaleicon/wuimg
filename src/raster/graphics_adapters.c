#include "common.h"

#include "unpack.h"
#include "graphics_adapters.h"

static void interleave_pal1(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t w, const size_t h, const uint8_t planes) {
	const size_t row_len = scanline_length(w, 1, 1);
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
const size_t w, const size_t h, const uint8_t planes) {
	const size_t row_len = scanline_length(w, 1, 1);
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
const size_t w, const uint8_t ch) {
	for (size_t z = 0; z < ch; ++z) {
		strip_spread(dst + z, src + w*z, w, ch);
	}
}

void vga_interleave(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t w, const size_t h, const uint8_t ch, const uint8_t bitdepth,
const bool paletted) {
	switch (bitdepth) {
	case 1:
		if (paletted) {
			interleave_pal1(dst, src, w, h, ch);
		} else {
			interleave_nopal1(dst, src, w, h, ch);
		}
		break;
	case 8:
		interleave8(dst, src, w, ch);
		break;
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
