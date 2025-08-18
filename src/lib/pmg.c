// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "misc/endian.h"
#include "lib/pmg.h"

/* Print Magic Graphic */

struct wu_st pmg_decode(struct wuptr mem, struct wuimg *img) {
	mem.ptr += 24;
	mem.len -= 24;
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
		14      u16     Height
		16      u8      ???[8]
		24      u8      RLEStream
	*/

	const uint8_t magic[] = {'P', 'M', 'G', 'R', 'A', 'F'};
	if (mem.len <= 24) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(mem.ptr, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	img->w = buf_endian16(mem.ptr + 10, little_endian) + 1;
	img->h = buf_endian16(mem.ptr + 14, little_endian) + 1;
	img->channels = 1;
	img->bitdepth = 1;
	img->attr = pix_inverted;
	return WU_OK;
}
