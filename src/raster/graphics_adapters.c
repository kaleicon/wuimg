// SPDX-License-Identifier: 0BSD
#include <string.h>

#include "misc/math.h"
#include "raster/graphics_adapters.h"

static void interleave_byte(uint8_t *restrict dst, const uint8_t *restrict src,
const uint8_t planes, const size_t plane_stride, const size_t pos, const size_t bits) {
	uint8_t buf[8] = {0};
	for (uint8_t z = 0; z < planes; ++z) {
		const uint8_t byte = src[pos + z*plane_stride];
		for (uint8_t bit = 0; bit < bits; ++bit) {
			buf[bit] |= ((byte >> (7-bit)) & 1) << z;
		}
	}
	memcpy(dst + pos*8, buf, bits);
}

static void interleave_row8_with_stride(uint8_t *restrict dst,
const uint8_t *restrict src, const size_t w, const uint8_t planes,
const size_t stride) {
	const size_t items = w/8;
	const size_t remain = w%8;
	for (size_t i = 0; i < items; ++i) {
		interleave_byte(dst, src, planes, stride, i, 8);
	}
	if (remain) {
		interleave_byte(dst, src, planes, stride, items, remain);
	}
}

void bitplane_interleave_row8(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t w, const uint8_t planes, const align_t align) {
	interleave_row8_with_stride(dst, src, w, planes,
		strip_length(w, 1, align));
}

static void interleave_dword(uint8_t *restrict dst, const uint8_t *restrict src,
const uint8_t planes, const size_t plane_stride, const size_t spread,
const size_t pos, const size_t bits) {
	uint8_t buf[8*4] = {0};
	for (uint8_t z = 0; z < planes; ++z) {
		const uint8_t byte = src[pos + z*plane_stride];
		for (uint8_t bit = 0; bit < bits; ++bit) {
			buf[bit*spread + (z/8)] |= ((byte >> (7-bit)) & 1) << (z%8);
		}
	}
	memcpy(dst + pos*8*spread, buf, bits*spread);
}

static void bitplane_interleave_row32(uint8_t *restrict dst,
const uint8_t *restrict src, const size_t w, const uint8_t planes,
const align_t align) {
	const size_t plane_stride = strip_length(w, 1, align);
	const size_t spread = 4;
	const size_t items = w/8;
	const size_t remain = w%8;
	for (size_t i = 0; i < items; ++i) {
		interleave_dword(dst, src, planes, plane_stride, spread, i, 8);
	}
	if (remain) {
		interleave_dword(dst, src, planes, plane_stride, spread, items, remain);
	}
}

void bitplane_interleave_row(void *restrict dst, const uint8_t *restrict src,
const size_t w, const uint8_t planes, const align_t align) {
	switch ((planes - 1) / 8) {
	case 0:
		bitplane_interleave_row8(dst, src, w, planes, align);
		break;
	case 2: case 3:
		bitplane_interleave_row32(dst, src, w, planes, align);
		break;
	}
}

void bitplane_interleave_plane(uint8_t *restrict dst,
const uint8_t *restrict src, const size_t w, const uint8_t planes,
const align_t align, const size_t h) {
	const size_t stride = strip_length(w, 1, align);
	const size_t size = stride*h;
	for (size_t y = 0; y < h; ++y) {
		interleave_row8_with_stride(dst + w*y, src + stride*y, w,
			planes, size);
	}
}

void bitplane_interleave_pack(uint8_t *restrict dst,
const uint8_t *restrict src, const size_t w, const uint8_t planes,
const align_t align) {
	const size_t stride = strip_length(w, 1, align);
	for (size_t x = 0; x < w; ++x) {
		for (size_t z = 0; z < planes; ++z) {
			const size_t pos = x*planes + z;
			const uint8_t byte = src[x/8 + stride*(planes - 1 - z)];
			dst[pos/8] |= ((byte >> (7 - x%8)) & 0x01)
				<< (7 - pos%8);
		}
	}
}

static int unpack_ykj_chroma(const uint8_t *src) {
	const unsigned n = (src[0] & 0x07u) | ((src[1] & 0x07u) << 3);
	return (int)((n ^ 0x20) - 0x20); // Propagate sign
}

void v9958_ykj_to_grb(upack1555_t *dst, const uint8_t *restrict src,
const size_t dwords, const struct palette *yae) {
	/*
		G = Y + K
		R = Y + J
		B = 5*Y/4 - J/2 - K/4
	 * Where
		Y is an unsigned 5-bit int
		K and J are signed 6-bit ints
		Division is floored (a.k.a. integer division)
		G, R, and B are clamped to [0, 0x1f]
	*/
	for (size_t i = 0; i < dwords; ++i) {
		const int k = unpack_ykj_chroma(src + i*4);
		const int j = unpack_ykj_chroma(src + i*4 + 2);
		for (size_t p = 0; p < 4; ++p) {
			const size_t pos = i*4 + p;
			const int y = src[pos] >> 3;
			int g, r, b;
			if (yae && (y & 1)) {
				g = yae->color[y/2].r;
				r = yae->color[y/2].g;
				b = yae->color[y/2].b;
			} else {
				g = iclamp(y + k, 0, 0x1f);
				r = iclamp(y + j, 0, 0x1f);
				b = iclamp(y*5/4 - j/2 - k/4, 0, 0x1f);
			}
			dst[pos] = (upack1555_t)(b << 10 | r << 5 | g);
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
