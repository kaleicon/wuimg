// SPDX-License-Identifier: 0BSD
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

static inline size_t base_lzss(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len, const uint8_t ch,
const uint32_t *off_table) {
	size_t d = 0;
	size_t s = 0;
	while (d < dst_len && s < src_len) {
		int flags = src[s];
		++s;
		for (int i = 0; i < 8; ++i, flags <<= 1) {
			if (flags & 0x80) {
				if (src_len - s < ch || dst_len - d < ch) {
					return d;
				}
				memcpy(dst + d, src + s, ch);
				s += ch;
				d += ch;
			} else {
				size_t cnt, offset;
				if (off_table) {
					if (s >= src_len) {
						return d;
					}
					const uint8_t tmp = src[s];
					++s;
					cnt = (tmp >> 4) + 2;
					if (dst_len - d < cnt) {
						return d;
					}
					offset = off_table[tmp & 0x0f];
					memrepeat_or_zero(dst, d, offset, cnt);
				} else {
					if (src_len - s < 2) {
						return d;
					}
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
		memcpy(offs, src, sizeof(offs));
		endian_loop32(offs, little_endian, ARRAY_LEN(offs));
		for (size_t k = 0; k < ARRAY_LEN(offs); ++k) {
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

size_t pdt_decode(const struct pdt_desc *desc, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}

	uint8_t color_ch = (desc->version == pdt10) ? 3 : 1;

	struct sewing_machine sew;
	strip_sew_init(&sew, img->data, (const struct raster_pal *)desc->pal,
		img->w, img->h, color_ch, img->align_sh, desc->mask_offset);

	size_t written = pick_decode(sew.color.ptr, sew.color.len,
		desc->mp.mem + desc->mp.pos, desc->mp.len - desc->mp.pos,
		desc->version);
	if (written && desc->mask_offset) {
		if (!strip_sew_alloc_alpha(&sew)) {
			return 0;
		}

		written += alpha_decode(sew.alpha.ptr, sew.alpha.len,
			desc->mp.mem + desc->mask_offset,
			desc->mp.len - desc->mask_offset);
		strip_sew_alpha(&sew);
		strip_sew_free_alpha(&sew);
	}
	return written;
}

enum wu_error pdt_parse_header(struct pdt_desc *desc, struct wuimg *img) {
	/* PDT header (after magic bytes):
		Offset  Type    Name
		0       u32     FileSize
		4       u32     Width
		8       u32     Height
		12      u32     ???[2]
		20      u32     MaskOffset
		24
	*/
	const uint8_t *buf = mp_next_slice(&desc->mp, 24);
	if (!buf) {
		return wu_unexpected_eof;
	}
	desc->mask_offset = buf_endian32(buf + 20, little_endian);
	if (desc->mask_offset >= desc->mp.len) {
		desc->mask_offset = 0;
	}
	img->w = buf_endian32(buf + 4, little_endian);
	img->h = buf_endian32(buf + 8, little_endian);
	img->bitdepth = 8;
	img->layout = pix_bgra;
	img->alpha = alpha_associated;
	if (desc->version == pdt11) {
		const size_t pal_size = 256 * 4;
		buf = mp_next_slice(&desc->mp, pal_size);
		if (!buf) {
			return wu_unexpected_eof;
		}

		if (desc->mask_offset) {
			img->channels = 4;
			desc->pal = buf;
		} else {
			img->channels = 1;
			struct raster_pal *pal = wuimg_palette_init(img);
			if (!pal) {
				return wu_alloc_error;
			}
			memcpy(pal, buf, pal_size);
			img->alpha = alpha_ignore;
		}
	} else {
		img->channels = (desc->mask_offset) ? 4 : 3;
	}
	return wuimg_verify(img);
}

enum wu_error pdt_open_mem(struct pdt_desc *desc, const struct map_info *map) {
	*desc = (struct pdt_desc) {
		.mp = mp_map(*map),
	};
	const uint8_t *buf = mp_next_slice(&desc->mp, 8);
	if (buf) {
		const uint8_t init[] = {'P', 'D', 'T', '1'};
		if (!memcmp(init, buf, sizeof(init)) && !memchk(buf + 5, 0, 3)) {
			switch (buf[4]) {
			case pdt10: case pdt11:
				desc->version = buf[4];
				return wu_ok;
			}
		}
		return wu_invalid_signature;
	}
	return wu_unexpected_eof;
}
