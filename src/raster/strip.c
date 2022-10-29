#include <stdlib.h>
#include <string.h>

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


static inline void scale_inline(void *dst, const void *src,
const size_t width, const uint64_t mul, const uint8_t depth, const uint64_t add) {
	for (size_t x = 0; x < width; ++x) {
		switch (depth) {
		case 8:
			;uint8_t *da = dst;
			const uint8_t *sa = src;
			da[x] = (uint8_t)(((sa[x] + add) * mul) >> depth);
			break;
		case 16:
			;uint16_t *db = dst;
			const uint16_t *sb = src;
			db[x] = (uint16_t)(((sb[x] + add) * mul) >> depth);
			break;
		case 32:
			;uint32_t *dc = dst;
			const uint32_t *sc = src;
			dc[x] = (uint32_t)(((sc[x] + add) * mul) >> depth);
			break;
		}
	}
}

static void design8(void *dst, const void *src,
const size_t w, const struct scale_info i) {
	scale_inline(dst, src, w, i.mul, 8, i.add);
}
static void design16(void *dst, const void *src,
const size_t w, const struct scale_info i) {
	scale_inline(dst, src, w, i.mul, 16, i.add);
}
static void design32(void *dst, const void *src,
const size_t w, const struct scale_info i) {
	scale_inline(dst, src, w, i.mul, 32, i.add);
}

static void scale8(void *dst, const void *src,
const size_t w, const struct scale_info i) {
	scale_inline(dst, src, w, i.mul, 8, 0);
}
static void scale16(void *dst, const void *src,
const size_t w, const struct scale_info i) {
	scale_inline(dst, src, w, i.mul, 16, 0);
}
static void scale32(void *dst, const void *src,
const size_t w, const struct scale_info i) {
	scale_inline(dst, src, w, i.mul, 32, 0);
}

void strip_scale(void *dst, const void *src,
const size_t width, const struct scale_info info, const bool design) {
	if (design) {
		switch (info.bitdepth) {
		case 8: design8(dst, src, width, info); break;
		case 16: design16(dst, src, width, info); break;
		case 32: design32(dst, src, width, info); break;
		}
	} else if (info.scale) {
		switch (info.bitdepth) {
		case 8: scale8(dst, src, width, info); break;
		case 16: scale16(dst, src, width, info); break;
		case 32: scale32(dst, src, width, info); break;
		}
	} else if (dst != src) {
		memcpy(dst, src, width * (info.bitdepth/8));
	}
}

struct scale_info strip_scale_info(const uint64_t maxval,
const uint8_t bitdepth) {
	const uint64_t range = ~0u >> (32 - bitdepth);
	return (struct scale_info) {
		.bitdepth = bitdepth,
		.scale = maxval != range,
		.size_shift = (uint8_t)(ulog2(umax(bitdepth, 8) - 1) - 2),
		.mul = (range << bitdepth) / maxval + 1,
		.add = maxval/2 + 1,
	};
}


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
		memcpy(dst + x, color + x, (x + 1 < w) ? 4 : 3);
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
const struct raster_pal *pal, const size_t w, const size_t h, const uint8_t ch,
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
