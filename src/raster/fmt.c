// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/math.h"
#include "raster/fmt.h"

size_t fmt_load_raster(struct wuimg *img, FILE *ifp) {
	return fread(img->data, 1, wuimg_size(img), ifp);
}

struct wu_st fmt_load_raster_st(struct wuimg *img, FILE *ifp) {
	return wuerr_partial(fmt_load_raster(img, ifp), wuimg_size(img));
}

bool fmt_will_swap(const struct fmt_swap_info info) {
	switch (info.depth) {
	case 16: case 24: case 32: case 64:
		return which_end() != info.e;
	}
	return false;
}

void fmt_swap(void *restrict data, const size_t len,
const struct fmt_swap_info info) {
	const enum endianness e = info.e;
	switch (info.depth) {
	case 16: endian_loop16(data, e, len/2); break;
	case 24: endian_loop24(data, e, len/3); break;
	case 32: endian_loop32(data, e, len/4); break;
	case 64: endian_loop64(data, e, len/8); break;
	}
}

size_t fmt_load_raster_callback(struct wuimg *img, FILE *ifp,
fmt_load_callback_t fn, void *restrict ptr) {
	const size_t stride = wuimg_stride(img);
	const size_t l = zumax(stride, img->h);
	const size_t s = zumin(stride, img->h);
	size_t acc = 0;
	for (size_t y = 0; y < s; ++y) {
		void *row = img->data + l*y;
		size_t read = fread(row, 1, l, ifp);
		acc += read;
		fn(row, read, ptr);
	}
	return acc;
}

static void swap_cb_wrap(void *restrict data, const size_t len,
void *restrict ptr) {
	const struct fmt_swap_info *info = ptr;
	fmt_swap(data, len, *info);
}

size_t fmt_load_raster_swap(struct wuimg *img, FILE *ifp, const enum endianness e) {
	struct fmt_swap_info info = {.e = e, .depth = img->bitdepth};
	return fmt_will_swap(info)
		? fmt_load_raster_callback(img, ifp, swap_cb_wrap, &info)
		: fmt_load_raster(img, ifp);
}


struct wu_st fmt_load_pal_bitrange(struct palette *pal, enum fmt_pal_type type,
size_t nmemb, FILE *ifp, uint8_t range) {
	const size_t size = (size_t)type;
	unsigned char *buf = (unsigned char *)pal->color + (4 - size) * nmemb;
	if (fread(buf, size*nmemb, 1, ifp)) {
		if (size == 3) {
			if (range == 8) {
				palette_from_rgb8(pal, buf, nmemb);
			} else {
				palette_from_rgb8_bitrange(pal, buf, nmemb,
					range);
			}
		}
		return wuok();
	}
	return WUERR_HERE(wu_unexpected_eof);;
}

enum wu_error fmt_load_pal(FILE *ifp, struct palette *pal,
const enum fmt_pal_type type, const size_t nmemb) {
	return fmt_load_pal_bitrange(pal, type, nmemb, ifp, 8).st;
}

enum wu_error fmt_sigcmp_mem(const unsigned char *restrict sig,
const size_t size, struct mparser *mp) {
	const uint8_t *buf = mp_slice(mp, size);
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
