// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/aliaspix.h"
#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/mem.h"

// http://www.martinreddy.net/gfx/2d/PIX.txt
struct wu_st aliaspix_decode(const struct aliaspix_desc *desc,
struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return WUERR_HERE(wu_alloc_error);
	}

	const size_t dims = img->w * img->h;
	struct mparser mp = desc->mp;
	const struct wuptr src = mp_remaining(&mp);
	return wuerr_partial(
		decomp_topbyterle(img->data, dims, src.ptr, src.len, img->channels),
		dims);
}

struct wu_st aliaspix_init(struct aliaspix_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	*desc = (struct aliaspix_desc) {.mp = mp_wuptr(mem)};
	const uint8_t *hdr = mp_slice(&desc->mp, 10);
	if (hdr) {
		img->w = buf_endian16(hdr, big_endian);
		img->h = buf_endian16(hdr + 2, big_endian);
		desc->x = buf_endian16(hdr + 4, big_endian);
		desc->y = buf_endian16(hdr + 6, big_endian);
		switch (buf_endian16(hdr + 8, big_endian)) {
		case 8: img->channels = 1; break;
		case 24: img->channels = 3; break;
		default: return wuerr(wu_invalid_header, "depth is not 8 nor 24");
		}
		img->bitdepth = 8;
		img->layout = pix_bgra;
		return WUERR_HERE(wuimg_verify(img));
	}
	return WUERR_HERE(wu_unexpected_eof);
}
