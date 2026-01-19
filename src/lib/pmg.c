// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "misc/endian.h"
#include "lib/pmg.h"

/* Print Magic Graphic and Card*/

static size_t rle_offset(const struct wuptr mem) {
	return mem.ptr[2] == 'G' ? 24 : 32;
}

struct wu_st pmg_decode(struct wuptr mem, struct wuimg *img) {
	const size_t start = rle_offset(mem);
	mem.ptr += start;
	mem.len -= start;
	size_t s = 0;
	size_t d = 0;
	const size_t dst_len = wuimg_size(img);
	const uint8_t flag = 0xbf;
	while (mem.len - s && dst_len - d) {
		const uint8_t c = mem.ptr[s];
		++s;
		if (c == flag) {
			if (mem.len - s < 2) {
				break;
			}
			const size_t cnt = mem.ptr[s] + 1;
			const uint8_t val = mem.ptr[s+1];
			if (dst_len - d < cnt) {
				break;
			}
			s += 2;
			memset(img->data + d, val, cnt);
			d += cnt;
		} else {
			img->data[d] = c;
			++d;
		}
	}
	return wuerr_partial(d, dst_len);
}

struct wu_st pmg_init(const struct wuptr mem, struct wuimg *img) {
	/* PMG header:
		Offset  Type    Name
		0       char    Magic[6]     // "PMGRAF"
		6       u16     Version?
		8       u16     X?           // Always 0
		10      u16     Width        // Bias of -1
		12      u16     Y?           // Always 0
		14      u16     Height       // Bias of -1
		16      u8      ???[8]
		24

	 * PMC header:
		0       char    Magic[6]     // "PMCARD"
		6       u16     Version?     // Always 0
		8       u16     Width        // No bias
		10      u16     Height
		12      u16     ???[6]       // Width and Height repeated?
		24      u8      Zeros[8]
		32

	 * Afterwards comes the RLE stream.
	*/

	const uint8_t graf[] = {'P', 'M', 'G', 'R', 'A', 'F'};
	const uint8_t card[] = {'P', 'M', 'C', 'A', 'R', 'D'};
	if (mem.len <= 24) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (!memcmp(mem.ptr, graf, sizeof(graf))) {
		img->w = buf_endian16l(mem.ptr + 10) + 1;
		img->h = buf_endian16l(mem.ptr + 14) + 1;
	} else if (!memcmp(mem.ptr, card, sizeof(card))) {
		img->w = buf_endian16l(mem.ptr + 8);
		img->h = buf_endian16l(mem.ptr + 10);
		if (mem.len <= 32) {
			return WUERR_HERE(wu_unexpected_eof);
		}
	} else {
		return WUERR_HERE(wu_invalid_signature);
	}
	img->channels = 1;
	img->bitdepth = 1;
	img->cs.invert = true;
	return WU_OK;
}
