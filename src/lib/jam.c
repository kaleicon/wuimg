// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "raster/fmt.h"
#include "lib/jam.h"

/* Aladdin JAM format
https://moddingwiki.shikadi.net/wiki/JAM_Format
*/

struct wu_st jam_decode(struct mparser mp, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return WUERR_HERE(wu_alloc_error);
	}

	const struct wuptr src = mp_remaining(&mp);
	const size_t dst_len = wuimg_size(img);
	uint8_t *dst = img->data;
	size_t s = 0;
	size_t d = 0;
	uint8_t flags;
	while (src.len - s > 1 && (flags = src.ptr[s]) != 0) {
		++s;
		unsigned len = flags;
		if ((flags & 0x80)) {
			len -= 0x7f;
			if (dst_len - d < len) {
				break;
			}
			memcpy(dst + d, src.ptr + s, len);
			s += len;
		} else {
			if ((flags & 0x40)) {
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
			uint8_t color = src.ptr[s];
			++s;
			memset(dst + d, color, len);
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
	uint16_t w = buf_endian16(hdr + 6, little_endian);
	uint16_t h = buf_endian16(hdr + 8, little_endian);
	const uint16_t direction = buf_endian16(hdr + 10, little_endian);
	const uint16_t depth = buf_endian16(hdr + 12, little_endian);
	const uint16_t elems = buf_endian16(hdr + 14, little_endian);
	if (memcmp(sig, hdr, sizeof(sig))
	|| (direction != jam_horizontal && direction != jam_vertical)
	|| depth != 8 || !elems || elems > 256*3) {
		return WUERR_HERE(wu_invalid_header);
	}
	const bool vert = direction == jam_vertical;
	img->w = vert ? h : w;
	img->h = vert ? w : h;
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 6;
	img->rotate = vert ? 1 : 0;
	img->mirror = vert;

	const uint8_t *pal = mp_slice(mp, elems);
	if (!pal) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct wu_st st = wuimg_palette_from_buf(img, 3, elems/3, pal);
	if (wu_isok(st)) {
		return wuimg_verify_st(img);
	}
	return st;
}
