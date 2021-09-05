#include <stdlib.h>

#include "../common.h"

#include "raster.h"
#include "unpack.h"
#include "graphics_adapters.h"

static void interleave_pal1(uint8_t *restrict dst, const uint8_t *restrict src,
const struct raster_desc *desc, const size_t lines, const size_t scanline) {
	const size_t plane_len = scanline * lines;
	for (size_t y = 0; y < lines; ++y) {
		const uint8_t *s = src + y*scanline;
		uint8_t *d = dst + y * desc->w;
		for (size_t x = 0; x < desc->w; ++x) {
			int val = 0;
			for (size_t z = 0; z < desc->ch; ++z) {
				const uint8_t byte = s[z*plane_len + x/8];
				val |= (bool)(byte & (0x80 >> (x%8))) << z;
			}
			d[x] = (uint8_t)val;
		}
	}
}

static void interleave_nopal1(uint8_t *restrict dst, const uint8_t *restrict src,
const struct raster_desc *desc, const size_t lines, const size_t scanline) {
	const size_t plane_len = scanline * lines;
	for (size_t z = 0; z < desc->ch; ++z) {
		for (size_t y = 0; y < lines; ++y) {
			const uint8_t *s = src + plane_len*z + scanline*y;
			uint8_t *d = dst + y * desc->w + z;
			for (size_t x = 0; x < desc->w; ++x) {
				const uint8_t byte = s[x/8];
				d[x*desc->ch] = byte & (0x80 >> (x%8)) ? 0xff : 0x00;
			}
		}
	}
}

static void interleave8(uint8_t *restrict dst, const uint8_t *restrict src,
const struct raster_desc *desc, const size_t lines, const size_t scanline) {
	const size_t dst_row = desc->w * desc->ch;
	const size_t plane_len = scanline * lines;
	for (size_t z = 0; z < desc->ch; ++z) {
		for (size_t y = 0; y < lines; ++y) {
			strip_spread(dst + y*dst_row + z, src + plane_len*z + scanline*y,
				desc->w, desc->ch);
		}
	}
}

void vga_interleave(uint8_t *dst, const uint8_t *restrict src,
const struct raster_desc *desc, const size_t lines, const size_t scanline) {
	switch (desc->bitdepth) {
	case 1:
		if (desc->palette) {
			interleave_pal1(dst, src, desc, lines, scanline);
		} else {
			interleave_nopal1(dst, src, desc, lines, scanline);
		}
		break;
	case 8:
		interleave8(dst, src, desc, lines, scanline);
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
	const size_t bright = idx >> 3;
	const size_t r = ((idx >> 1) & 2) | bright;
	const size_t g = ((idx     ) & 2) | bright;
	const size_t b = ((idx << 1) & 2) | bright;

	const size_t brown_circuit = idx == 6 ? 0x55 : 0;
	return (struct pix_rgba8) {
		.r = (uint8_t)(r * 0x55),
		.g = (uint8_t)(g * 0x55 - brown_circuit),
		.b = (uint8_t)(b * 0x55),
		.a = 0xff,
	};
}
