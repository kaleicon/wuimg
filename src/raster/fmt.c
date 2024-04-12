// SPDX-License-Identifier: 0BSD
#include <stdlib.h>
#include <string.h>

#include "misc/file.h"
#include "misc/math.h"
#include "raster/fmt.h"

size_t fmt_load_raster(struct wuimg *img, FILE *ifp) {
	return fread(img->data, 1, wuimg_size(img), ifp);
}

static bool will_swap(const enum endianness e, const uint8_t word_depth) {
	if (e != which_end()) {
		switch (word_depth) {
		case 16: case 24: case 32: case 64: return true;
		}
	}
	return false;
}

size_t fmt_load_raster_swap_depth(struct wuimg *img, FILE *ifp,
const enum endianness e, const uint8_t word_depth) {
	if (!will_swap(e, word_depth)) {
		return fmt_load_raster(img, ifp);
	}
	const size_t stride = wuimg_stride(img);
	const size_t l = zumax(stride, img->h);
	const size_t s = zumin(stride, img->h);
	size_t acc = 0;
	for (size_t y = 0; y < s; ++y) {
		void *row = img->data + l*y;
		acc += file_endian_read_bytes(row, l, ifp, word_depth, e);
	}
	return acc;
}

size_t fmt_load_raster_swap(struct wuimg *img, FILE *ifp, const enum endianness e) {
	return fmt_load_raster_swap_depth(img, ifp, e, img->bitdepth);
}

enum wu_error fmt_load_pal(FILE *ifp, struct raster_pal *pal,
const enum fmt_pal_type type, const size_t entries) {
	const size_t elen = (size_t)type;
	unsigned char *buf = (unsigned char *)pal + (4 - elen) * entries;
	if (fread(buf, elen * entries, 1, ifp)) {
		if (elen == 3) {
			raster_pal_from_rgb8(pal, buf, entries);
		}
		return wu_ok;
	}
	return wu_unexpected_eof;
}

enum wu_error fmt_sigcmp_mem(const unsigned char *restrict sig,
const size_t size, struct mparser *mp) {
	const uint8_t *buf = mp_next_slice(mp, size);
	if (buf) {
		return !memcmp(buf, sig, size) ? wu_ok : wu_invalid_signature;
	}
	return wu_unexpected_eof;
}

enum wu_error fmt_sigcmp(const unsigned char *restrict sig, const size_t size,
FILE *ifp) {
	unsigned char buf[8];
	for (size_t off = 0; off < size; off += sizeof(buf)) {
		const size_t len = zumin(size - off, sizeof(buf));
		if (!fread(buf, len, 1, ifp)) {
			return wu_unexpected_eof;
		}
		if (memcmp(buf, sig + off, len)) {
			return wu_invalid_signature;
		}
	}
	return wu_ok;
}
