// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "spooky.h"

/* Sprite and Run-Length encoded versions of the Spooky Sprites formats.
 * The uncompressed variant is implemented in auto.c

 * Doc:
http://cd.textfiles.com/atarilibrary/atari_cd07/GRAPHICS/PAINT/SPOOKY4/SPOOKY.TXT
*/

static void endian_bufcpy(uint16_t *dst, const uint8_t *restrict src,
const size_t nmemb) {
	for (size_t i = 0; i < nmemb; ++i) {
		dst[i] = buf_endian16(src + i*2, big_endian);
	}
}

static size_t tre_rle(uint16_t *dst, const size_t dst_len,
const uint8_t *restrict src, size_t src_len) {
	size_t d = 0;
	size_t s = 0;
	// Prevent out of bounds read on the second iteration
	if (!src[s]) {
		return s;
	}
	for (bool rle = false; s + 1 < src_len; rle = !rle) {
		size_t val = src[s];
		++s;
		if (val == 0xff) {
			if (s + 2 > src_len) {
				break;
			}
			val += buf_endian16(src + s, big_endian);
			s += 2;
		}

		if (d + val > dst_len) {
			break;
		}
		if (rle) {
			memwordset(dst + d, dst + d - 1, 2, val);
		} else {
			if (s + val > src_len) {
				break;
			}
			endian_bufcpy(dst + d, src + s, val);
			s += val*2;
		}
		d += val;
	}
	return s;
}

size_t tre_decode(const struct tre_desc *desc, struct wuimg *img) {
	size_t w = 0;
	if (wuimg_alloc_noverify(img)) {
		struct mparser mp = desc->mp;
		struct wuptr src = mp_remaining(&mp);
		w = tre_rle((uint16_t *)img->data, img->w * img->h,
			src.ptr, src.len);
	}
	return w;
}

static enum wu_error common_setup(struct wuimg *img, const bool alpha) {
	img->channels = 1;
	img->bitdepth = alpha ? 24 : 16;
	img->layout = pix_bgra;
	// Never thought something as bizarre as 0x1565 would ever happen
	if (wuimg_bitfield_from_id(img, alpha << 12 | 0x565)) {
		return wuimg_verify(img);
	}
	return wu_alloc_error;
}

enum wu_error tre_parse(struct tre_desc *desc, struct wuimg *img) {
	/* True Color Encoded header (after magic bytes):
		Offset  Type    Name
		0       u16     Width
		2       u16     Height
		4       u32     NrChunks
		8       Chunks[]
	*/
	const uint8_t *header = mp_slice(&desc->mp, 8);
	if (header) {
		img->w = buf_endian16(header, big_endian);
		img->h = buf_endian16(header + 2, big_endian);
		desc->chunks = buf_endian32(header + 4, big_endian);
		return common_setup(img, false);
	}
	return wu_unexpected_eof;
}

enum wu_error tre_init(struct tre_desc *desc, const struct wuptr mem) {
	*desc = (struct tre_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t magic[] = {'t', 'r', 'e', '1'};
	return fmt_sigcmp_mem(magic, sizeof(magic), &desc->mp);
}

static size_t sprite_unpack(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, size_t src_len, const size_t w,
const uint16_t xres) {
	/* TRS Sprite:
		Offset  Type    Name
		0       u16     NrChunks
		2       struct  Chunks[NrChunks]

	 * TRS Chunk:
		Offset  Type    Name
		0       u16     Skip         // Advance this many screen *bytes*
		2       u16     Len          // Nr of Pixels minus 1
		4       u16     Pixels[Len]

	 * The sprite is meant to be drawn on a screen that's `xres` pixels
	 * wide, with appropiate skips to get from one line to the next.
	 * All the defined data should fall between [0, Width] and [0, Height].
	*/
	size_t s = 0;
	if (src_len > 2) {
		const uint16_t nr = buf_endian16(src, big_endian);
		s += 2;

		size_t screen_ptr = 0;
		const uint8_t shl = which_end() == big_endian ? 8 : 0;
		for (uint16_t chunk = 0; chunk < nr && s + 4 < src_len; ++chunk) {
			/* Not sure if skips can be odd, but it'd be too much
			 * trouble to handle that so lets pretend they won't. */
			const uint16_t skip = buf_endian16(src + s, big_endian) / 2;
			const size_t len = buf_endian16(src + s + 2, big_endian) + 1;
			s += 4;
			screen_ptr += skip;

			const size_t x = screen_ptr % xres;
			const size_t y = screen_ptr / xres;
			size_t d = y*w + x;
			if (d + len > dst_len || s + len*2 > src_len) {
				break;
			}
			for (size_t i = 0; i < len; ++i) {
				uint32_t p = 1 << 16 // alpha bit
					| buf_endian16(src + s, big_endian);
				p <<= shl;
				memcpy(dst + d*3, &p, 3);
				++d;
				s += 2;
			}
			screen_ptr += len;
		}
	}
	return s;
}

size_t trs_get_image(const struct trs_desc *desc, struct wuimg *img,
const uint16_t i) {
	size_t w = 0;
	if (wuimg_alloc_noverify(img)) {
		const uint8_t *sprite = desc->sprites + 10*i;
		const uint32_t unpacked = buf_endian32(sprite + 2, big_endian);
		const uint32_t packed = buf_endian32(sprite + 6, big_endian);
		const uint32_t pos = packed ? packed : unpacked;
		struct mparser mp = desc->mp;
		if (pos && pos < mp.len) {
			mp.pos = pos;
			struct wuptr src = mp_remaining(&mp);
			const size_t len = img->w * img->h;
			if (packed) {
				w = sprite_unpack(img->data, len, src.ptr,
					src.len, img->w, desc->xres);
			} else {
				w = zumin(len, src.len);
				uint16_t *dst = (uint16_t *)img->data;
				endian_bufcpy(dst, src.ptr, w);
			}
		}
	}
	return w;
}

enum wu_error trs_set_image(struct trs_desc *desc, struct wuimg *img,
const uint16_t i) {
	/* TRS SpriteLoc struct:
		Offset  Type    Name
		0       u8      Width
		1       u8      Height
		2       u32     UnpackOffset // 0 if data is not raw
		6       u32     PackOffset   // 0 if data is raw
		10
	 * Both offsets relative to the start of file.
	*/
	const uint8_t *sprite = desc->sprites + 10*i;
	img->w = sprite[0];
	img->h = sprite[1];
	const bool packed = buf_endian32(sprite + 6, big_endian);
	return common_setup(img, packed);
}

enum wu_error trs_init(struct trs_desc *desc, const struct wuptr mem) {
	/* TRS header (after magic bytes):
		Offset  Type    Name
		0       u8      Magic[4]
		4       u16     NrSprites
		6       u16     Version    // 1
		8       u16     XRes
		10      SpriteLoc[]
	*/
	*desc = (struct trs_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t *header = mp_slice(&desc->mp, 10);
	if (header) {
		const uint8_t magic[] = {'T', 'C', 'S', 'F'};
		const uint16_t version = buf_endian16(header + 6, big_endian);
		if (!memcmp(header, magic, sizeof(magic)) && version == 1) {
			desc->nr = buf_endian16(header + 4, big_endian);
			desc->xres = buf_endian16(header + 8, big_endian);
			desc->sprites = mp_slice(&desc->mp, 10*desc->nr);
			if (desc->xres) {
				return desc->sprites ? wu_ok : wu_unexpected_eof;
			}
		}
		return wu_invalid_header;
	}
	return wu_unexpected_eof;
}
