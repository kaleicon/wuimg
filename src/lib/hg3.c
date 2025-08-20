// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <stdlib.h>

#include <zlib.h>

#include "misc/bit.h"
#include "misc/math.h"
#include "raster/fmt.h"
#include "hg3.h"

/* Format documentation:
https://github.com/trigger-segfault/TriggersTools.CatSystem2/wiki/HG%E2%80%903-Image
https://github.com/trigger-segfault/TriggersTools.CatSystem2/wiki/HG%E2%80%90X-ProcessImage
*/

static const size_t STDINFO_LEN = 56;

static uint32_t biject(const uint32_t val) {
	const uint32_t repl = 0x01010101;
	return ((val & ~repl) >> 1) ^ ((val & repl) * 0xff);
}

static void plane_mix(uint32_t *dst, const uint8_t *restrict plane,
const size_t plane_len) {
	/* The recipe is
	 *  1) Read a byte from each of the four planes
	 *  2) Split each byte into 4 2-bit group.
	 *  3) Place each group into the bytes of a 32-bit word. Highest
	 *     group in the highest byte, first planes into highest bits.
	 *     Example:
	        Plane0: 11010011
	        Output: 11000000 01000000 00000000 11000000
	        -       Byte 3   Byte 2   Byte 1   Byte 0

	        Plane1: 01110101
	        Output: 11010000 01110000 00010000 11010000
	        -       Byte 3   Byte 2   Byte 1   Byte 0

	 *  4) For each resulting byte, if it's odd, do (255 - byte/2), else,
	 *     byte/2. This converts from a zigzag signed encoding to two's
	 *     complement.
	 *  5) Write in little-endian order to the output buffer. The result
	 *     should be in BGR/BGRA logical order... Though I've never tested
	 *     any three-channel images. */

	const uint32_t repl = (1 << 30) | (1 << 20) | (1 << 10) | (1 << 0);
	const uint32_t mask = 0xc0c0c0c0;
	for (size_t pos = 0; pos < plane_len; ++pos) {
		uint32_t val = 0;
		for (uint8_t z = 0; z < 4; ++z) {
			val |= ((plane[z*plane_len + pos] * repl) & mask) >> (z*2);
		}
		/* Channel order becomes ARGB/BRGB in the code above on
		 * little-endian machines, so reverse again. */
		dst[pos] = endian32(biject(val), big_endian);
	}
}

static size_t multi_add(uint8_t *bytes, size_t pos, const size_t diff,
const size_t limit) {
	/* If the stars align, add 4 bytes at a time. */
	if (diff % 4 == 0 && pos + 10 < limit) {
		while (pos % 4) {
			bytes[pos] += bytes[pos - diff];
			++pos;
		}

		uint32_t *dword = (uint32_t *)bytes;
		size_t dpos = pos/4;
		while (dpos < limit/4) {
			dword[dpos] = uadd8_32(dword[dpos],
				dword[dpos - diff/4]);
			++dpos;
		}
		pos = dpos*4;
	}
	while (pos < limit) {
		bytes[pos] += bytes[pos - diff];
		++pos;
	}
	return pos;
}

static void decode_delta(struct wuimg *img, const size_t img_size) {
	const uint8_t ch = img->channels;
	const size_t stride = img->w * ch;

	/* For the first row, add the previous pixel to the current one. For
	 * the rest, add from the pixel above. */
	multi_add(img->data, multi_add(img->data, ch, ch, stride),
		stride, img_size);
}

static uint8_t * decode_zrle(uint8_t *restrict ext, const size_t ext_len,
uint8_t *restrict ctrl, size_t ctrl_len, const size_t data_len) {
	struct bitstrm bs;
	bitstrm_from_bytes(&bs, ctrl, ctrl_len);

	bool copy = bitstrm_lsb_next(&bs);
	const size_t stream_len = bitstrm_lsb_gamma_one(&bs);
	if (stream_len != data_len) {
		return NULL;
	}

	size_t size = bitstrm_lsb_gamma_one(&bs);
	if (copy && size >= data_len) {
		return ext;
	}

	uint8_t *data = calloc(data_len, 1);
	if (data) {
		size_t e = 0;
		size_t d = 0;
		for (;;) {
			if (copy) {
				if (d + size > data_len || e + size > ext_len) {
					break;
				}
				memcpy(data + d, ext + e, size);
				e += size;
			}
			d += size;
			copy = !copy;
			if (bs.eof) {
				break;
			}
			size = bitstrm_lsb_gamma_one(&bs);
		}
	}
	return data;
}

bool hg3_decode(const struct hg3_desc *desc, struct wuimg *img) {
	/* img0000 tag structure:
		Offset  Size    Name
		0       struct  TagHeader
		16      u32     StartRow // Always 0
		20      u32     EndRow   // Always the image height
		24      u32     ExtentCompSize
		28      u32     ExtentOrigSize
		32      u32     CtrlCompSize
		36      u32     CtrlOrigSize
		40      u8      ExtentDeflateStream[CompSize]
		--      u8      CtrlDeflateStream[CompSize]
	*/

	// The tag ID has already been read, so substract 8 from the offsets
	struct mparser mp = desc->image;
	const uint8_t *tag = mp_slice(&mp, 32);
	if (!tag) {
		return false;
	}

	uLong extent_comp = buf_endian32(tag + 16, little_endian);
	uLong extent_orig = buf_endian32(tag + 20, little_endian);
	uLong ctrl_comp = buf_endian32(tag + 24, little_endian);
	uLong ctrl_orig = buf_endian32(tag + 28, little_endian);

	const struct wuptr zext = mp_avail(&mp, extent_comp);
	const struct wuptr zctrl = mp_avail(&mp, ctrl_comp);
	if (!zctrl.len) {
		return false;
	}

	size_t uncomp_size = extent_orig + ctrl_orig;
	uint8_t *buf = malloc((size_t)uncomp_size);
	if (!buf) {
		return false;
	}

	uint8_t *extent = buf;
	uncompress(extent, &extent_orig, zext.ptr, zext.len);
	uint8_t *ctrl = buf + extent_orig;
	uncompress(ctrl, &ctrl_orig, zctrl.ptr, zctrl.len);
	uncomp_size = extent_orig + ctrl_orig;

	const size_t img_size = wuimg_size(img);
	uint8_t *planes = decode_zrle(extent, extent_orig, ctrl, ctrl_orig,
		img_size);
	if (planes != buf) {
		free(buf);
	}

	bool ok = false;
	if (planes) {
		if (wuimg_alloc_noverify(img)) {
			/* Not sure what's supposed to happen if the image
			 * size is not a multiple of 4. */
			plane_mix((uint32_t *)img->data, planes, img_size/4);
			decode_delta(img, img_size);
			ok = true;
		}
		free(planes);
	}
	return ok;
}

enum wu_error hg3_parse_image(struct hg3_desc *desc, struct wuimg *img) {
	const uint8_t *stdinfo = mp_slice(&desc->image, STDINFO_LEN);
	if (!stdinfo) {
		return wu_unexpected_eof;
	}
	const uint8_t name[8] = "stdinfo\0";
	const uint32_t size = buf_endian32(stdinfo + 8, little_endian);
	if (memcmp(stdinfo, name, sizeof(name)) || size != STDINFO_LEN) {
		return wu_invalid_header;
	}

	img->w = buf_endian32(stdinfo + 16, little_endian);
	img->h = buf_endian32(stdinfo + 20, little_endian);
	const uint32_t depth = buf_endian32(stdinfo + 24, little_endian);
	switch (depth) {
	case 24: case 32:
		img->channels = (uint8_t)(depth / 8);
		break;
	default: return wu_invalid_header;
	}
	img->bitdepth = 8;
	img->layout = pix_bgra;
	img->mirror = true;
	img->alpha = buf_endian32(stdinfo + 44, little_endian)
		? alpha_unassociated : alpha_ignore;

	desc->x = (int32_t)buf_endian32(stdinfo + 28, little_endian);
	desc->y = (int32_t)buf_endian32(stdinfo + 32, little_endian);
	desc->canvas_w = buf_endian32(stdinfo + 36, little_endian);
	desc->canvas_h = buf_endian32(stdinfo + 40, little_endian);

	const uint8_t *tag = mp_slice(&desc->image, 8);
	if (!tag) {
		return wu_unexpected_eof;
	}

	const uint8_t id[8] = "img0000\0";
	if (!memcmp(tag, id, sizeof(id))) {
		return wuimg_verify(img);
	}
	return wu_unsupported_feature;
}

enum wu_error hg3_next_image(struct hg3_desc *desc) {
	/* ImageEntry structure
		Offset  Size    Name
		0       u32     OffsetToNext // If 0, this is the last ImageEntry
		4       u32     ID
		8       struct  Tags[]

	 * TagHeader
		0       char    Name[8]
		8       u32     OffsetToNext // If 0, this is the last tag
		12      u32     Size         // Actual size of this tag
		16

	 * stdinfo tag structure
		0       struct  TagHeader
		16      u32     Width
		20      u32     Height
		24      u32     Depth
		28      i32     XOffset
		32      i32     YOffset
		36      u32     CanvasWidth
		40      u32     CanvasHeight
		44      u32     Transparency
		48      u32     XCenter
		52      u32     YCenter
		56
	*/
	const uint8_t *entry_header = mp_slice(&desc->mp, 8);
	if (!entry_header) {
		return wu_unexpected_eof;
	}
	const size_t next = buf_endian32(entry_header, little_endian);
	size_t len;
	if (next < 8) {
		len = desc->mp.len - desc->mp.pos;
	} else {
		len = next - 8;
	}

	const struct wuptr m = mp_avail(&desc->mp, len);
	if (m.len > STDINFO_LEN + 8) {
		desc->image = mp_mem(m.len, m.ptr);
		return wu_ok;
	}
	return wu_unexpected_eof;
}

enum wu_error hg3_open(struct hg3_desc *desc, const struct wuptr mem) {
	/* Overall structure:
		Header
		ImageEntry
		 · stdinfo tag
		 · Compressed image tags
		ImageEntry[...]

	 * HG-3 header:
		Offset  Size    Name
		0       u8      Identifier[4] // "HG-3"
		4       u32     HeaderSize    // 12
		8       u32     Version       // 0x300
		12
	*/
	*desc = (struct hg3_desc) {
		.mp = mp_wuptr(mem)
	};
	const uint8_t id[4] = "HG-3";
	const enum wu_error st = fmt_sigcmp_mem(id, sizeof(id), &desc->mp);
	if (st == wu_ok) {
		const uint8_t *header = mp_slice(&desc->mp, 8);
		if (header) {
			const uint32_t size = buf_endian32(header, little_endian);
			const uint32_t version = buf_endian32(header + 4, little_endian);
			return (size == 0x0c && version == 0x300)
				? wu_ok : wu_invalid_header;
		}
		return wu_unexpected_eof;
	}
	return st;
}
