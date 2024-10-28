// SPDX-License-Identifier: 0BSD
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/math.h"
#include "strip.h"

align_t align_from_int(const size_t alignment) {
	return (align_t)zulog2(alignment);
}

size_t strip_base(const size_t width, const uint8_t bitdepth) {
	return (width * bitdepth + 7) / 8;
}

size_t strip_length(const size_t width, const uint8_t bitdepth,
const align_t align_sh) {
	const size_t a = ~0lu << align_sh;
	return (strip_base(width, bitdepth) + ~a) & a;
}

size_t strip_padding(const size_t width, const uint8_t bitdepth,
const align_t align_sh) {
	const size_t bytes = strip_base(width, bitdepth);
	const size_t a = ~0lu << align_sh;
	return ((bytes + ~a) & a) - bytes;
}

align_t strip_alignment(const size_t stride, const size_t width,
const uint8_t bitdepth) {
	const size_t base = strip_base(width, bitdepth);
	if (stride >= base) {
		const size_t diff = stride - base;
		if (diff) {
			return (align_t)(zulog2(diff) + 1);
		}
		return 0;
	}
	return -1;
}

void strip_spread(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t ch) {
	for (size_t x = 0; x < width; ++x) {
		dst[x*ch] = src[x];
	}
}

static void sew_pal_alpha8(struct pix_rgba8 *dst,
const uint8_t *restrict entries, const uint8_t *restrict alpha, const size_t w,
const struct palette *pal) {
	for (size_t x = 0; x < w; ++x) {
		memcpy(dst + x, pal->color + entries[x], 4);
		dst[x].a = alpha[x];
	}
}

static void sew_gray8_alpha8(uint8_t *restrict dst,
const uint8_t *restrict gray, const uint8_t *restrict alpha, const size_t w) {
	for (size_t x = 0; x < w; ++x) {
		dst[x*2] = gray[x];
		dst[x*2 + 1] = alpha[x];
	}
}

static void sew_color24_alpha8(struct pix_rgba8 *dst,
const struct pix_rgb8 *color, const uint8_t *restrict alpha, const size_t w) {
	for (size_t x = 0; x < w; ++x) {
		memmove(dst + x, color + x, (x + 1 < w) ? 4 : 3);
		dst[x].a = alpha[x];
	}
}

void strip_handsew_alpha(void *dst, const void *color,
const void *alpha, const size_t w, const struct palette *pal,
const uint8_t ch) {
	switch (ch) {
	case 1:
		if (pal) {
			sew_pal_alpha8(dst, color, alpha, w, pal);
		} else {
			sew_gray8_alpha8(dst, color, alpha, w);
		}
		break;
	case 3:
		sew_color24_alpha8(dst, color, alpha, w);
		break;
	case 4:
		strip_spread((uint8_t *)dst + 3, alpha, w, ch);
		break;
	}
}

static uint8_t * row_ptr(struct sewing_clothe *clothe, const size_t y) {
	return clothe->ptr + y*clothe->stride;
}

void strip_sew_alpha(struct sewing_machine *sew) {
	size_t w = sew->w;
	size_t h = sew->h;
	if (sew->compact) {
		w *= h;
		h = 1;
	}
	for (size_t y = 0; y < h; ++y) {
		strip_handsew_alpha(row_ptr(&sew->dst, y),
			row_ptr(&sew->color, y), row_ptr(&sew->alpha, y),
			w, sew->pal, sew->ch);
	}
}

static struct sewing_clothe set_plane(void *ptr, const size_t w, const size_t h,
const align_t align) {
	const size_t stride = strip_length(w, 8, align);
	return (struct sewing_clothe) {
		.ptr = ptr,
		.stride = stride,
		.len = stride * h,
	};
}

void strip_sew_free_alpha(struct sewing_machine *sew) {
	free(sew->alpha.ptr);
}

bool strip_sew_alloc_alpha(struct sewing_machine *sew) {
	sew->alpha.ptr = malloc(sew->alpha.len);
	return sew->alpha.ptr;
}

void strip_sew_init(struct sewing_machine *sew, void *restrict dst,
const struct palette *pal, const size_t w, const size_t h, const uint8_t ch,
const align_t align, const bool will_sew) {
	const uint8_t out_ch = (will_sew)
		? ((pal || ch > 2) ? 4 : 2)
		: ch;
	*sew = (struct sewing_machine) {
		.out_ch = out_ch,
		.ch = ch,
		.compact = align == 0,
		.pal = pal,
		.w = w,
		.h = h,
		.dst = set_plane(dst, w * out_ch, h, align),
		.color = set_plane(dst, w * ch, h, align),
		.alpha = set_plane(NULL, w, h, align),
	};
	sew->color.ptr += sew->dst.len - sew->color.len;
}
