// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "raster/fmt.h"
#include "lib/jam.h"

/* Aladdin JAM format
https://moddingwiki.shikadi.net/wiki/JAM_Format
*/

struct wu_st jam_decode(struct mparser mp, struct wuimg *img) {
	const struct wuptr src = mp_remaining(&mp);
	const size_t dst_len = wuimg_size(img);
	uint8_t *dst = img->data;
	size_t s = 0;
	size_t d = 0;
	while (src.len - s >= 2 && dst_len - d) {
		unsigned len = src.ptr[s];
		++s;
		if ((len & 0x80)) {
			len -= 0x80 - 1;
			if (dst_len - d < len || src.len - s < len) {
				break;
			}
			memcpy(dst + d, src.ptr + s, len);
			s += len;
		} else {
			if ((len & 0x40)) {
				if (src.len - s < 2) {
					break;
				}
				len = (((len ^ 0x40) << 8) | src.ptr[s]);
				++s;
			}
			++len;
			if (dst_len - d < len) {
				break;
			}
			memset(dst + d, src.ptr[s], len);
			++s;
		}
		d += len;
	}
	return wuerr_partial(d, dst_len);
}

enum jam_direction {
	jam_horizontal = 8,
	jam_vertical = 9,
};

struct wu_st jam_parse(struct mparser *mp, struct wuimg *img) {
	/* JAM structure (after ID):
		Offset  Type    Name
		0       u8      ID[4]
		4       u16     FileSize
		6       u16     Width
		8       u16     Height
		10      u16     Direction
		12      u16     Bitdepth?
		14      u16     PalElems
		16      u8      Palette[PalElems]
		--      u8      CompressedData
	*/
	const uint8_t *hdr = mp_slice(mp, 16);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint8_t sig[] = {'X', 'C', 'O', 'M'};
	const uint16_t direction = buf_endian16l(hdr + 10);
	const uint16_t depth = buf_endian16l(hdr + 12);
	const uint16_t elems = buf_endian16l(hdr + 14);
	if (memcmp(sig, hdr, sizeof(sig))
	|| (direction != jam_horizontal && direction != jam_vertical)
	|| depth != 8 || !elems || elems > 256*3) {
		return WUERR_HERE(wu_invalid_header);
	}
	const bool vert = direction == jam_vertical;
	const uint16_t w = buf_endian16l(hdr + 6);
	const uint16_t h = buf_endian16l(hdr + 8);
	img->w = vert ? h : w;
	img->h = vert ? w : h;
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 6;
	img->rotate = vert ? 1 : 0;
	img->mirror = vert;

	const uint8_t *pal = mp_slice(mp, elems);
	return pal
		? wuimg_palette_from_buf(img, 3, elems/3, pal)
		: WUERR_HERE(wu_unexpected_eof);
}
