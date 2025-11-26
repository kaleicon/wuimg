// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/endian.h"
#include "misc/mem.h"
#include "qoi.h"

enum qoi_ops {
	qoi_op_index = 0,
	qoi_op_diff = 1,
	qoi_op_luma = 2,
	qoi_op_run = 3,

	qoi_op_rgb = 62,
	qoi_op_rgba = 63,
};

static uint8_t hash_pxl(const struct pix_rgba8 p) {
	return (p.r*3 + p.g*5 + p.b*7 + p.a*11) & 0x3f;
}

static void witness_pxl(struct pix_rgba8 seen[static 64],
const struct pix_rgba8 cur) {
	memcpy(seen + hash_pxl(cur), &cur, sizeof(cur));
}

static size_t qoi_inner(struct mparser mp, struct wuimg *img,
const size_t dst_len) {
	const struct wuptr src = mp_remaining(&mp);
	struct pix_rgba8 seen[64] = {0};
	size_t s = 0;
	size_t d = 0;
	struct pix_rgba8 cur = {0, 0, 0, 255};

	if (s < src.len) {
		/* Cache initial color if the first instruction is a pixel run
		https://github.com/phoboslab/qoi/issues/258
		*/
		const uint8_t c = src.ptr[s];
		const uint8_t lo = qoi_op_run << 6;
		if (c >= lo && c < (lo | qoi_op_rgb)) {
			witness_pxl(seen, cur);
		}
	}
	while (s < src.len) {
		const uint8_t c = src.ptr[s];
		++s;
		const uint8_t arg = c & 0x3f;
		switch (c >> 6) {
		case qoi_op_index:
			memcpy(&cur, seen + arg, sizeof(cur));
			break;
		case qoi_op_diff:
			cur.r += (uint8_t)((arg >> 4) - 2);
			cur.g += (uint8_t)(((arg >> 2) & 0x03) - 2);
			cur.b += (uint8_t)((arg & 0x03) - 2);
			witness_pxl(seen, cur);
			break;
		case qoi_op_luma:
			if (s >= src.len) {
				return d;
			}
			const uint8_t rb = src.ptr[s];
			++s;
			const int g = arg - 32 - 8;
			cur.r += (uint8_t)(g + (rb >> 4));
			cur.g += (uint8_t)(g + 8);
			cur.b += (uint8_t)(g + (rb & 0x0f));
			witness_pxl(seen, cur);
			break;
		case qoi_op_run:
			switch (arg) {
			uint8_t ch;
			case qoi_op_rgb:
				ch = 3;
				if (ch > src.len - s) {
					return d;
				}
				memcpy(&cur, src.ptr + s, ch);
				s += ch;
				break;
			case qoi_op_rgba:
				ch = 4;
				if (ch > src.len - s) {
					return d;
				}
				memcpy(&cur, src.ptr + s, ch);
				s += ch;
				break;
			default:
				ch = img->channels;
				const size_t run = arg + 1;
				if (run*ch > dst_len - d) {
					return d;
				}
				memwordset(img->data + d, &cur, ch, run);
				d += run * ch;
				continue;
			}
			witness_pxl(seen, cur);
			break;
		}
		if (d >= dst_len) {
			break;
		}
		memcpy(img->data + d, &cur, 4);
		d += img->channels;
	}
	return d;
}

struct wu_st qoi_decode(const struct mparser *mp, struct wuimg *img) {
	const size_t dst_len = wuimg_size(img);
	// Add 1 byte of padding so we can use a faster 4-byte memcpy
	img->data = malloc(dst_len + (bool)(img->channels == 3));
	return img->data
		? wuerr_partial(qoi_inner(*mp, img, dst_len), dst_len)
		: WUERR_HERE(wu_alloc_error);
}

struct wu_st qoi_parse(struct mparser *mp, struct wuimg *img,
const struct wuptr mem) {
	/* QOI header:
		Offset  Type    Name
		0       u8      Magic[4]
		4       u32     Width
		8       u32     Height
		12      u8      Channels
		13      u8      IsLinearRGB
		14
	*/
	*mp = mp_wuptr(mem);
	const uint8_t magic[4] = {'q', 'o', 'i', 'f'};
	const uint8_t *header = mp_slice(mp, 14);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(header, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	switch (header[12]) {
	case 3: case 4:
		img->w = buf_endian32b(header + 4);
		img->h = buf_endian32b(header + 8);
		img->channels = header[12];
		img->bitdepth = 8;
		if (header[9]) {
			img->cs.transfer = cicp_transfer_linear;
		}
		return wuimg_verify_st(img);
	}
	return wuerr(wu_invalid_header, "image channels is neither 3 or 4");
}
