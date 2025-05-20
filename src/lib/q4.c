// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "q4.h"

/* MAJYO's Q4 format.
 * There doesn't seem to be a written spec, just the really messy q4toppm.c
 * code, which is full of quirks like mysterious constants, unnecessary arrays,
 * nul-delimiting buffers and omitting bounds checks despite 0 still being
 * valid data, etc.

 * Beneath all that is a very simple Run-Length then LZW encoded paletted
 * image format.
*/

struct q4_block {
	struct wuptr data;
	size_t orig_len;
	uint16_t *off;
	uint16_t off_alloc;
	uint16_t codes;
	uint16_t comp_len;
	uint8_t rle[0x10000];
};

time_t q4_approximate_date(const struct q4_desc *desc) {
	/* The header timestamp seems somewhat approximated by the number of
	 * half-minutes since the DOS epoch. This stays within 3 months before
	 * and 5 days after from the modification times in XLD4_Images.zip.
	 * TODO: Is there a better choice? */
	const time_t dos_epoch = 315543600; // 1980-01-01
	return (time_t)desc->creation_date * 30 + dos_epoch;
}

static bool read_block(struct q4_block *block, struct mparser *mp) {
	const uint8_t *head = mp_slice(mp, 6);
	if (head) {
		const uint16_t comp_len = buf_endian16(head, little_endian);
		block->data = mp_avail(mp, comp_len);
		block->orig_len = buf_endian16(head + 4, little_endian) * 2;
		block->comp_len = comp_len;
		block->codes = buf_endian16(head + 2, little_endian);
	}
	return head;
}

#define MAX_RLE_READ 5
static size_t rledec(uint8_t *restrict dst, const uint8_t *restrict src,
const size_t dst_len, size_t src_len) {
	size_t s = 0;
	size_t d = 0;
	uint8_t saved_val = 0;
	uint8_t end[MAX_RLE_READ*2];
	for (;;) {
		const size_t read = memccpy_cur(dst + d, src + s, 0x10,
			dst_len - d, src_len - s);
		d += read;
		s += read;

		if (s + MAX_RLE_READ > src_len) {
			if (src == end) {
				break;
			}
			src = mem_bufswitch(src, &s, &src_len, end, sizeof(end));
		}
		uint8_t r = src[s+1];
		s += 2;
		if (r == 0) {
			saved_val = src[s];
			r = src[s+1];
			s += 2;
		}
		size_t repeat = r * 0x11u + src[s];
		++s;
		if (d + repeat > dst_len) {
			break;
		}
		memset(dst + d, saved_val, repeat);
		d += repeat;
	}
	return d;
}

static size_t lzwdec(uint8_t *restrict orig_dst, struct q4_block *block,
struct bitstrm bs) {
	const size_t MIN_LZW_CODE = 0x13;
	uint8_t *rle = block->rle;
	uint16_t *off = block->off - MIN_LZW_CODE;
	uint8_t bits = 3;
	size_t cur = MIN_LZW_CODE;
	size_t r = 0;
	const size_t max_pos = (size_t)block->codes + MIN_LZW_CODE;
	while (bs.pos < bs.len && cur < max_pos) {
		uint16_t idx = (uint16_t)bitstrm_msb_adv_max25(&bs, bits);
		switch (idx) {
		case 0: goto exit;
		case 1:
			if (bits >= 16) {
				goto exit;
			}
			++bits;
			continue;
		case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
		case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf:
		case 0x10: case 0x11: case 0x12:
			if (r == sizeof(block->rle)) {
				goto exit;
			}
			off[cur] = (uint16_t)r;
			rle[r] = (uint8_t)(idx - 2);
			++r;
			break;
		default:
			if (idx >= cur) {
				goto exit;
			}
			off[cur] = (uint16_t)r;
			const uint16_t base = off[idx];
			const uint16_t len = off[idx+1] - base;
			if (r + len + 1 > sizeof(block->rle)) {
				goto exit;
			}
			memcpy(rle + r, rle + base, len);
			rle[r+len] = rle[base+len];
			r += len + 1;
			break;
		}
		++cur;
	}
exit:
	return rledec(orig_dst, rle, block->orig_len, r);
}

static size_t decode_block(uint8_t *restrict dst, struct q4_block *block) {
	if (block->off_alloc < block->codes) {
		free(block->off);
		const size_t alloc = zumin(0xffff, block->codes + block->codes/4);
		block->off = small_malloc(alloc, sizeof(*block->off));
		if (!block->off) {
			block->off_alloc = 0;
			return 0;
		}
		block->off_alloc = (uint16_t)alloc;
	}
	return lzwdec(dst, block, bitstrm_from_wuptr(block->data));
}

static bool write_palette(struct mparser *mp, struct q4_block *block,
struct wuimg *img) {
	if (!read_block(block, mp)
	|| block->orig_len != 0x60 || block->comp_len != block->data.len) {
		return false;
	}

	struct palette *pal = img->u.palette;
	uint8_t *buf = (uint8_t *)(pal->color + 16);
	const size_t w = decode_block(buf, block);
	for (uint8_t i = 0; i < w/6; ++i) {
		const unsigned z = (i & 0x8u) | ((i >> 2) & 0x1u) | (i & 0x3u) << 1;
		pal->color[i] = (struct pix_rgba8) {
			// 4-bit colors stored in 16-bits little-endian words
			.r = buf[6*z + 1]*0x11u,
			.g = buf[6*z + 3]*0x11u,
			.b = buf[6*z + 5]*0x11u,
			.a = 0xff,
		};
	}
	return w;
}

static size_t write_image(struct mparser *mp, struct q4_block *block,
struct wuimg *img) {
	const size_t max = img->w * img->h;
	size_t d = 0;
	while (read_block(block, mp)) {
		if (!block->data.len || max - d < block->orig_len) {
			break;
		}
		d += decode_block(img->data + d, block);
	}
	return d;
}

size_t q4_decode(const struct q4_desc *desc, struct wuimg *img) {
	size_t w = 0;
	if (wuimg_alloc_noverify(img)) {
		struct q4_block *block = malloc(sizeof(*block));
		if (block) {
			block->off = NULL;
			block->off_alloc = 0;
			struct mparser mp = desc->mp;
			if (write_palette(&mp, block, img)) {
				for (uint8_t k = 0; k < desc->skip; ++k) {
					// TODO: What are these blocks?
					if (!read_block(block, &mp)) {
						return 0;
					}
				}
				w = write_image(&mp, block, img);
			}
			free(block->off);
			free(block);
		}
	}
	return w;
}

enum wu_error q4_info(struct wuimg *img) {
	img->w = 640;
	img->h = 400;
	img->channels = 1;
	img->bitdepth = 8;
	return wuimg_palette_init(img)
		? wuimg_verify(img) : wu_alloc_error;
}

enum wu_error q4_open(struct q4_desc *desc, const struct wuptr mem) {
	/* Q4 format:
		Offset  Type    Name
		0       u8      EOF          // 0x1a
		1       u8      ???          // < 2 if ID2 == 2
		2       u8      ID2
		3       u8      ???          // < 2 if ID2 == 2
		4       u8      BlockFlags   // 0x01 == Palette?
		5       u24     CreationTime
		8       u24     FileSize
		11      u8      Magic[5]     // "MAJYO"
		16      DataBlock[]

	 * DataBlock:
		Offset  Type    Name
		0       u16     DataLen
		2       u16     LZWCodes  // Total number of LZW codes
		4       u16     UncompLen // In u16 units
		6       u8[]    Data
	*/
	*desc = (struct q4_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t *head = mp_slice(&desc->mp, 16);
	if (!head) {
		return wu_unexpected_eof;
	}
	const uint8_t sig[] = {'M', 'A', 'J', 'Y', 'O'};
	if (!memcmp(head+11, sig, sizeof(sig))) {
		if (head[2] != 2 && (head[1] > 1 || head[3] > 1)) {
			return wu_invalid_header;
		}
		desc->skip = (bool)(head[4] & 0x2u) + (bool)(head[4] & 0x8u);
		desc->creation_date = buf_endian24(head + 5, little_endian);
		return wu_ok;
	}
	return wu_invalid_signature;
}
