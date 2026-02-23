// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/aliaspix.h"
#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/mem.h"

// http://www.martinreddy.net/gfx/2d/PIX.txt
struct wu_st aliaspix_decode(const struct aliaspix_desc *desc,
struct wuimg *img) {
	const size_t dims = img->w * img->h;
	const struct wuptr src = desc->data;
	return wuerr_partial(
		decomp_topbyterle(img->data, dims, src.ptr, src.len, img->channels),
		dims);
}

struct wu_st aliaspix_init(struct aliaspix_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* AliasPIX header:
		Offset  Type    Name
		0       u16     Width
		2       u16     Height
		4       u16     X
		6       u16     Y
		8       u16     PixDepth
	*/
	if (mem.len > 10) {
		const uint8_t *hdr = mem.ptr;
		*desc = (struct aliaspix_desc) {
			.data = {
				.ptr = mem.ptr + 10,
				.len = mem.len - 10,
			},
			.x = buf_endian16b(hdr + 4),
			.y = buf_endian16b(hdr + 6),
		};
		img->w = buf_endian16b(hdr);
		img->h = buf_endian16b(hdr + 2);
		switch (buf_endian16b(hdr + 8)) {
		case 8: img->channels = 1; break;
		case 24: img->channels = 3; break;
		default: return wuerr(wu_invalid_header, "depth is not 8 nor 24");
		}
		img->bitdepth = 8;
		img->layout = pix_bgra;
		return WU_OK;
	}
	return WUERR_HERE(wu_unexpected_eof);
}
