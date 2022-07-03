#include <string.h>

#include "strip.h"
#include "pal.h"

void strip_spread(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t width, const size_t ch) {
	for (size_t x = 0; x < width; ++x) {
		dst[x*ch] = src[x];
	}
}

static void sew_pal_alpha8(struct pix_rgba8 *dst,
const uint8_t *restrict entries, const uint8_t *restrict alpha, const size_t w,
const struct raster_pal *pal) {
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
		memcpy(dst + x, color + x, (x < (w - 1)) ? 4 : 3);
		dst[x].a = alpha[x];
	}
}

void strip_handsew_alpha(void *restrict dst, const void *restrict color,
const void *restrict alpha, const size_t w, const void *restrict pal,
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
		strip_spread(dst, alpha, w, ch);
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
const uint8_t align) {
	const size_t stride = scanline_length(w, 8, align);
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
const struct raster_pal *pal, const size_t w, const size_t h, const uint8_t ch,
const uint8_t align, const bool will_sew) {
	const uint8_t out_ch = (will_sew)
		? ((pal || ch > 2) ? 4 : 2)
		: ch;
	*sew = (struct sewing_machine) {
		.out_ch = out_ch,
		.ch = ch,
		.compact = align <= 1,
		.pal = pal,
		.w = w,
		.h = h,
		.dst = set_plane(dst, w * out_ch, h, align),
		.color = set_plane(dst, w * ch, h, align),
		.alpha = set_plane(NULL, w, h, align),
	};
	sew->color.ptr += sew->dst.len - sew->color.len;
}
