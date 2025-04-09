// SPDX-License-Identifier: 0BSD
#include "misc/bit.h"
#include "misc/endian.h"
#include "misc/mem.h"
#include "lib/skyroads.h"

/* SkyRoads LZS format
https://moddingwiki.shikadi.net/wiki/SkyRoads_Image_Format
https://moddingwiki.shikadi.net/wiki/SkyRoads_compression
*/

size_t skyroads_decode(struct mparser mp, struct wuimg *img) {
	const uint8_t *hdr = mp_slice(&mp, 3);
	if (!hdr) {
		return 0;
	}
	const uint8_t cbits = hdr[0];
	const uint8_t sbits = hdr[1];
	const uint8_t lbits = hdr[2];
	// Limit to the amount of bits we can read in a single call
	if (cbits + sbits > 23 || cbits + lbits > 23
	|| !wuimg_alloc_noverify(img)) {
		return 0;
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
	return d;
}

enum wu_error skyroads_parse(struct mparser *mp, struct wuimg *img,
struct wuptr map) {
	/* SkyRoads CMAP struct:
		Offset  Type    Name
		0       u8      ID[4]
		1       u8      Entries
		2       u8      Pal[Entries*3]
		--      u8      ???[Entries*2]

	 * PICT struct:
		0       u8      ID[4]
		4       u16     ???    // 0
		6       u16     Height // Note that Height comes first
		8       u16     Width
		10
	*/
	*mp = mp_wuptr(map);
	const uint8_t cmap[] = {'C', 'M', 'A', 'P'};
	const uint8_t *hdr = mp_slice(mp, 5);
	if (!hdr) {
		return wu_unexpected_eof;
	} else if (memcmp(hdr, cmap, sizeof(cmap))) {
		return wu_unknown_file_type;
	}
	const uint8_t elems = hdr[4];
	if (!elems) {
		return wu_invalid_header;
	}

	hdr = mp_slice(mp, elems*5 + 10u);
	if (!hdr) {
		return wu_unexpected_eof;
	}
	img->channels = 1;
	img->bitdepth = 8;
	struct palette *pal = wuimg_palette_init(img);
	if (pal) {
		palette_from_rgb8_bitrange(pal, hdr, elems, 6);
		hdr += elems*5;
		const uint8_t pict[] = {'P', 'I', 'C', 'T'};
		if (!memcmp(hdr, pict, sizeof(pict))) {
			img->w = buf_endian16(hdr + 8, little_endian);
			img->h = buf_endian16(hdr + 6, little_endian);
			return wuimg_verify(img);
		}
		return wu_invalid_header;
	}
	return wu_alloc_error;
}
