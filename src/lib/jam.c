// SPDX-License-Identifier: 0BSD
#include "raster/fmt.h"
#include "lib/jam.h"

/* Aladdin JAM format
https://moddingwiki.shikadi.net/wiki/JAM_Format
*/

size_t jam_decode(struct mparser mp, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return 0;
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
	return d;
}

enum jam_direction {
	jam_horizontal = 8,
	jam_vertical = 9,
};

enum wu_error jam_parse(struct mparser *mp, struct wuimg *img) {
	/* JAM structure (after ID):
		Offset  Type    Name
		0       u16     FileSize
		2       u16     Width
		4       u16     Height
		6       u16     Direction
		8       u16     Bitdepth?
		10      u16     PalElems
		12      u8      Palette[PalElems]
		--      u8      CompressedData
	*/
	const uint8_t *hdr = mp_slice(mp, 12);
	if (!hdr) {
		return wu_unexpected_eof;
	}

	uint16_t w = buf_endian16(hdr + 2, little_endian);
	uint16_t h = buf_endian16(hdr + 4, little_endian);
	const uint16_t direction = buf_endian16(hdr + 6, little_endian);
	const uint16_t depth = buf_endian16(hdr + 8, little_endian);
	const uint16_t elems = buf_endian16(hdr + 10, little_endian);
	if ((direction != jam_horizontal && direction != jam_vertical)
	|| depth != 8 || !elems || elems > 256*3) {
		return wu_invalid_header;
	}
	const bool vert = direction == jam_vertical;
	img->w = vert ? h : w;
	img->h = vert ? w : h;
	img->channels = 1;
	img->bitdepth = 8;
	img->rotate = vert ? 1 : 0;
	img->mirror = vert;

	hdr = mp_slice(mp, elems);
	if (!hdr) {
		return wu_unexpected_eof;
	}
	struct palette *pal = wuimg_palette_init(img);
	if (!pal) {
		return wu_alloc_error;
	}
	for (size_t i = 0; i < elems/3; ++i) {
		pal->color[i] = (struct pix_rgba8) {
			.r = (uint8_t)(hdr[i*3] * 0x41 >> 4),
			.g = (uint8_t)(hdr[i*3+1] * 0x41 >> 4),
			.b = (uint8_t)(hdr[i*3+2] * 0x41 >> 4),
			.a = 0xff,
		};
	}
	return wuimg_verify(img);
}

enum wu_error jam_identify(struct mparser *mp, const struct wuptr map) {
	const uint8_t sig[] = {'X', 'C', 'O', 'M'};
	*mp = mp_wuptr(map);
	return fmt_sigcmp_mem(sig, sizeof(sig), mp);
}
