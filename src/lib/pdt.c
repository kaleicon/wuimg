// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "raster/strip.h"
#include "pdt.h"

const char * pdt_version_str(const enum pdt_version version) {
	switch (version) {
	case pdt10: return "PDT10";
	case pdt11: return "PDT11";
	}
	return "???";
}

void pdt_cleanup(struct pdt_desc *desc) {
	palette_unref(desc->pal);
}

#define MAX_LZSS_READ (3*8 + 1)
static inline size_t base_lzss(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, size_t src_len, const uint8_t ch,
const uint32_t *off_table) {
	size_t d = 0;
	size_t s = 0;
	uint8_t end[MAX_LZSS_READ * 2];
	while (d < dst_len && s < src_len) {
		if (s + MAX_LZSS_READ > src_len) {
			if (src == end) {
				break;
			}
			src = mem_bufswitch(src, &s, &src_len, end, sizeof(end));
		}
		int flags = src[s];
		++s;
		for (int i = 0; i < 8; ++i, flags <<= 1) {
			if (flags & 0x80) {
				if (dst_len - d < ch) {
					return d;
				}
				memcpy(dst + d, src + s, ch);
				s += ch;
				d += ch;
			} else {
				size_t cnt, offset;
				if (off_table) {
					const uint8_t tmp = src[s];
					++s;
					cnt = (tmp >> 4) + 2;
					if (dst_len - d < cnt) {
						return d;
					}
					offset = off_table[tmp & 0x0f];
					memrepeat_or_zero(dst, d, offset, cnt);
				} else {
					if (ch == 3) {
						const size_t tmp = buf_endian16(
							src + s, little_endian);
						cnt = ((tmp & 0x0f) + 1) * ch;
						offset = ((tmp >> 4) + 1) * ch;
					} else {
						cnt = src[s] + 2;
						offset = src[s + 1] + 1;
					}
					if (d < offset || dst_len - d < cnt) {
						return d;
					}
					memrepeat(dst, d, offset, cnt);
					s += 2;
				}
				d += cnt;
			}
		}
	}
	return d;
}

static size_t alpha_decode(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len) {
	return base_lzss(dst, dst_len, src, src_len, 1, NULL);
}

static size_t color_decode(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len) {
	return base_lzss(dst, dst_len, src, src_len, 3, NULL);
}

static size_t pal_decode(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len) {
	uint32_t offs[0x10];
	if (src_len > sizeof(offs)) {
		for (size_t k = 0; k < ARRAY_LEN(offs); ++k) {
			offs[k] = buf_endian32(src + k*4, little_endian);
			if (!offs[k]) {
				return 0;
			}
		}
		return base_lzss(dst, dst_len, src + sizeof(offs),
			src_len - sizeof(offs), 1, offs);
	}
	return 0;
}

static size_t pick_decode(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len,
const enum pdt_version version) {
	if (version == pdt10) {
		return color_decode(dst, dst_len, src, src_len);
	}
	return pal_decode(dst, dst_len, src, src_len);
}

struct wu_st pdt_decode(const struct pdt_desc *desc, struct wuimg *img) {
	uint8_t color_ch = (desc->version == pdt10) ? 3 : 1;

	struct sewing_machine sew;
	strip_sew_init(&sew, img->data, desc->pal, img->w, img->h, color_ch,
		img->align_sh, desc->mask_offset);

	size_t total = sew.color.len;
	size_t written = pick_decode(sew.color.ptr, sew.color.len,
		desc->mp.mem + desc->mp.pos, desc->mp.len - desc->mp.pos,
		desc->version);
	if (written && desc->mask_offset) {
		if (!strip_sew_alloc_alpha(&sew)) {
			return wuerr(wu_alloc_error,
				"failed to allocate mask buffer");
		}

		total += sew.alpha.len;
		written += alpha_decode(sew.alpha.ptr, sew.alpha.len,
			desc->mp.mem + desc->mask_offset,
			desc->mp.len - desc->mask_offset);
		strip_sew_alpha(&sew);
		strip_sew_free_alpha(&sew);
	}
	return wuerr_partial(written, total);
}

struct wu_st pdt_init(struct pdt_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* PDT header:
		Offset  Type    Name
		0       u8      MagicAndVersion[8]
		8       u32     FileSize
		12      u32     Width
		16      u32     Height
		20      u32     ???[2]
		28      u32     MaskOffset
		32
	*/
	*desc = (struct pdt_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t *buf = mp_slice(&desc->mp, 32);
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint8_t init[] = {'P', 'D', 'T', '1'};
	if (memcmp(init, buf, sizeof(init)) || memchk(buf + 5, 0, 3)) {
		return WUERR_HERE(wu_invalid_signature);
	}
	switch (buf[4]) {
	case pdt10: case pdt11:
		desc->version = buf[4];
		break;
	default: return wuerr(wu_invalid_signature, "unknown PDT version");
	}

	desc->mask_offset = buf_endian32(buf + 28, little_endian);
	if (desc->mask_offset >= desc->mp.len) {
		desc->mask_offset = 0;
	}
	img->w = buf_endian32(buf + 12, little_endian);
	img->h = buf_endian32(buf + 16, little_endian);
	img->bitdepth = 8;
	img->layout = pix_bgra;
	img->alpha = alpha_associated;
	if (desc->version == pdt11) {
		const size_t pal_size = 256 * 4;
		buf = mp_slice(&desc->mp, pal_size);
		if (!buf) {
			return WUERR_HERE(wu_unexpected_eof);
		}

		struct palette *pal = palette_new();
		if (!pal) {
			return WUERR_HERE(wu_alloc_error);
		}
		memcpy(pal->color, buf, pal_size);
		if (desc->mask_offset) {
			img->channels = 4;
			desc->pal = pal;
		} else {
			img->channels = 1;
			img->alpha = alpha_ignore;
			wuimg_palette_set(img, pal);
		}
	} else {
		img->channels = (desc->mask_offset) ? 4 : 3;
	}
	return WU_OK;
}
