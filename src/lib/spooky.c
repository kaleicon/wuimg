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

static void copy_be(uint16_t *dst, const uint8_t *restrict src,
const size_t nmemb) {
	for (size_t i = 0; i < nmemb; ++i) {
		dst[i] = buf_endian16b(src + i*2);
	}
}

struct wu_st tre_decode(const struct tre_desc *desc, struct wuimg *img) {
	uint16_t *dst = (uint16_t *)img->data;
	const size_t dst_len = img->w * img->h;
	const struct wuptr src = desc->data;
	size_t d = 0;
	size_t s = 0;
	// Prevent out of bounds read on the second iteration
	if (src.len && src.ptr[s]) {
		for (bool set = false; s < src.len; set = !set) {
			size_t len = src.ptr[s];
			++s;
			if (len == 0xff) {
				if (src.len - s < 2) {
					break;
				}
				len += buf_endian16b(src.ptr + s);
				s += 2;
			}

			if (dst_len - d < len) {
				break;
			}
			if (set) {
				memset16(dst + d, dst + d - 1, len);
			} else {
				if (src.len - s < len*2) {
					break;
				}
				copy_be(dst + d, src.ptr + s, len);
				s += len*2;
			}
			d += len;
		}
	}
	return wuerr_partial(d, dst_len);
}

static struct wu_st common_setup(struct wuimg *img, const bool alpha) {
	img->channels = 1;
	img->bitdepth = alpha ? 24 : 16;
	img->layout = pix_bgra;
	// Never thought something as bizarre as 0x1565 would ever happen
	return wuimg_bitfield_from_id(img, (uint16_t)(alpha << 12 | 0x565))
		? WU_OK : WUERR_HERE(wu_alloc_error);
}

struct wu_st tre_parse(struct tre_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* True Color Encoded header:
		Offset  Type    Name
		0       u8      Magic[4]
		4       u16     Width
		6       u16     Height
		8       u32     NrChunks
		12      Chunks[]
	*/
	struct mparser mp = mp_wuptr(mem);
	const uint8_t magic[] = {'t', 'r', 'e', '1'};
	const uint8_t *header = mp_slice(&mp, 12);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(header, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	img->w = buf_endian16b(header + 4);
	img->h = buf_endian16b(header + 6);
	*desc = (struct tre_desc) {
		.chunks = buf_endian32b(header + 8),
		.data = mp_remaining(&mp),
	};
	return common_setup(img, false);
}

static struct wu_st sprite_unpack(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len, const size_t w,
const uint16_t xres) {
	/* TRS Sprite:
		Offset  Type    Name
		0       u16     NrChunks
		2       struct  Chunks[NrChunks+1]

	 * TRS Chunk:
		Offset  Type    Name
		0       u16     Skip         // Advance this many screen *bytes*
		2       u16     Len
		4       u16     Pixels[Len+1]

	 * The sprite is meant to be drawn on a screen that's `xres` pixels
	 * wide, with appropiate skips to get from one line to the next.
	 * All the defined data should fall between [0, Width] and [0, Height].
	*/
	if (src_len) {
		const size_t nr_chunk = buf_endian16b(src) + 1;
		size_t s = 1;

		size_t screen_ptr = 0;
		const uint8_t shl = which_end() == big_endian ? 8 : 0;
		size_t chunk = 0;
		while (chunk < nr_chunk && src_len - s > 2) {
			/* Not sure if skips can be odd, but it'd be too much
			 * trouble to handle that so lets pretend they won't. */
			const uint16_t skip = buf_endian16b(src + s*2) / 2;
			++s;
			const size_t len = buf_endian16b(src + s*2) + 1;
			++s;
			screen_ptr += skip;

			const size_t x = screen_ptr % xres;
			const size_t y = screen_ptr / xres;
			size_t d = y*w + x;
			if (d + len > dst_len || src_len - s < len) {
				break;
			}
			for (size_t i = 0; i < len; ++i) {
				uint32_t p = 1 << 16 // alpha bit
					| buf_endian16b(src + s*2);
				p <<= shl;
				memcpy(dst + d*3, &p, 3);
				++d;
				++s;
			}
			screen_ptr += len;
			++chunk;
		}
		return wuerr_partial(chunk, nr_chunk);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

struct wu_st trs_get_image(const struct trs_desc *desc, struct wuimg *img,
const uint16_t i) {
	const uint8_t *sprite = desc->sprites + 10*i;
	const uint32_t unpacked = buf_endian32b(sprite + 2);
	const uint32_t packed = buf_endian32b(sprite + 6);
	const uint32_t pos = packed ? packed : unpacked;
	struct mparser mp = desc->mp;

	if (pos < mp.len) {
		mp.pos = pos;
		struct wuptr src = mp_remaining(&mp);
		const size_t dims = img->w * img->h;
		src.len /= 2;
		if (packed) {
			return sprite_unpack(img->data, dims, src.ptr,
				src.len, img->w, desc->xres);
		}
		const size_t w = zumin(src.len, dims);
		copy_be((uint16_t *)img->data, src.ptr, w);
		return wuerr_partial(w, dims);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

struct wu_st trs_set_image(const struct trs_desc *desc, struct wuimg *img,
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
	const bool packed = buf_endian32b(sprite + 6);
	return common_setup(img, packed);
}

struct wu_st trs_parse(struct trs_desc *desc, const struct wuptr mem) {
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
		const uint16_t version = buf_endian16b(header + 6);
		if (!memcmp(header, magic, sizeof(magic)) && version == 1) {
			desc->nr = buf_endian16b(header + 4);
			desc->xres = buf_endian16b(header + 8);
			desc->sprites = mp_slice(&desc->mp, 10*desc->nr);
			if (desc->xres && desc->sprites) {
				return WU_OK;
			}
			return wuerr(wu_invalid_header, "no xres or sprites");
		}
		return WUERR_HERE(wu_invalid_signature);
	}
	return WUERR_HERE(wu_unexpected_eof);
}
