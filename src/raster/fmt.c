// SPDX-License-Identifier: 0BSD
#include <stdlib.h>
#include <string.h>

#include "misc/file.h"
#include "misc/math.h"
#include "raster/fmt.h"

size_t fmt_load_raster(struct wuimg *img, FILE *ifp, const enum endianness e) {
	if (e == which_end()) {
		return fread(img->data, 1, wuimg_size(img), ifp);
	}
	const size_t w = zumax(img->w, img->h);
	const size_t h = zumin(img->w, img->h);
	const size_t stride = strip_length(w * img->channels, img->bitdepth,
		img->align_sh);
	size_t acc = 0;
	for (size_t y = 0; y < h; ++y) {
		void *row = img->data + stride*y;
		acc += file_endian_read(row, stride, ifp, img->bitdepth, e);
	}
	return acc;
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
