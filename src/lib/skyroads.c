// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "misc/bit.h"
#include "misc/endian.h"
#include "misc/mem.h"
#include "lib/skyroads.h"

/* SkyRoads LZS format
https://moddingwiki.shikadi.net/wiki/SkyRoads_Image_Format
https://moddingwiki.shikadi.net/wiki/SkyRoads_compression
*/

struct wu_st skyroads_decode(struct mparser mp, struct wuimg *img) {
	const uint8_t *hdr = mp_slice(&mp, 3);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint8_t cbits = hdr[0];
	const uint8_t sbits = hdr[1];
	const uint8_t lbits = hdr[2];
	// Limit to the amount of bits we can read in a single call
	if (cbits + sbits > 23 || cbits + lbits > 23) {
		return wuerr(wu_unsupported_feature, "too many compression bits");
	} else if (!wuimg_alloc_noverify(img)) {
		return WUERR_HERE(wu_alloc_error);
	}

	const uint32_t cntmask = bit_set32(cbits);
	const size_t dst_len = wuimg_size(img);
	uint8_t *dst = img->data;
	struct bitstrm bs = bitstrm_from_wuptr(mp_remaining(&mp));
	size_t d = 0;
	while (d < dst_len) {
		uint32_t c = bitstrm_msb_peek_high25(&bs);
		uint32_t adv = 10;
		uint32_t cnt = 1;
		if (c >> 30 == 3) {
			dst[d] = (uint8_t)(c >> (30 - 8));
		} else {
			uint32_t dist, dist_bits;
			if (c >> 30 == 2) {
				c <<= 1;
				dist = 2u + (1 << sbits);
				dist_bits = lbits;
				adv = 2;
			} else {
				dist = 2;
				dist_bits = sbits;
				adv = 1;
			}
			adv += dist_bits + cbits;
			dist += (c >> (31 - dist_bits));
			cnt = (cntmask & (c >> (31 - (dist_bits + cbits)))) + 2;
			if (dst_len - d < cnt) {
				break;
			}
			memrepeat_or_zero(dst, d, dist, cnt);
		}
		d += cnt;
		bitstrm_seek(&bs, adv);
	}
	return wuerr_partial(d, dst_len);
}

struct wu_st skyroads_parse(struct mparser *mp, struct wuimg *img,
struct wuptr map) {
	/* SkyRoads CMAP struct:
		Offset  Type    Name
		0       u8      ID[4]
		4       u8      Entries
		5       u8      Pal[Entries*3]
		--      u8      ???[Entries*2]

	 * PICT struct:
		0       u8      ID[4]
		4       u16     ???    // 0
		6       u16     Height // Note that Height comes first
		8       u16     Width
		10
	*/
	*mp = mp_wuptr(map);
	const uint8_t *hdr = mp_slice(mp, 5);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint8_t cmap[] = {'C', 'M', 'A', 'P'};
	const uint8_t elems = hdr[4];
	if (memcmp(hdr, cmap, sizeof(cmap))) {
		return WUERR_HERE(wu_invalid_header);
	} else if (!elems) {
		return wuerr(wu_invalid_header, "no palette items");
	}

	hdr = mp_slice(mp, elems*5 + 10u);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 6;
	const struct wu_st st = wuimg_palette_from_buf(img, 3, elems, hdr);
	if (wu_isok(st)) {
		hdr += elems*5;
		const uint8_t pict[] = {'P', 'I', 'C', 'T'};
		if (!memcmp(hdr, pict, sizeof(pict))) {
			img->w = buf_endian16(hdr + 8, little_endian);
			img->h = buf_endian16(hdr + 6, little_endian);
			return wuimg_verify_st(img);
		}
		return WUERR_HERE(wu_invalid_header);
	}
	return st;
}
