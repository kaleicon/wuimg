// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/decomp.h"
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
	 *  2) Split each byte into four 2-bit groups
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
		dst[pos] = endian32b(biject(val));
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
	bitstrm_lsb_gamma_one(&bs); // result should equal `data_len`
	size_t size = bitstrm_lsb_gamma_one(&bs);
	if (copy && size >= data_len) {
		return ext;
	}

	uint8_t *data = calloc(data_len, 1);
	if (data) {
		size_t e = 0;
		size_t d = 0;
		while (data_len - d >= size) {
			if (copy) {
				if (ext_len - e < size) {
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
		memset(data + d, 0, data_len - d);
	}
	return data;
}

struct wu_st hg3_decode(const struct hg3_desc *desc, struct wuimg *img) {
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

	struct mparser mp = desc->image;
	const uint8_t id[8] = {'i', 'm', 'g', '0', '0', '0', '0', 0};
	const uint8_t *tag = mp_slice(&mp, 40);
	if (!tag) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(tag, id, sizeof(id))) {
		return wuerr(wu_unsupported_feature, "only img0000 tag supported");
	}

	size_t extent_comp = buf_endian32l(tag + 24);
	size_t extent_orig = buf_endian32l(tag + 28);
	size_t ctrl_comp = buf_endian32l(tag + 32);
	size_t ctrl_orig = buf_endian32l(tag + 36);

	const size_t uncomp_size = extent_orig + ctrl_orig;
	if (uncomp_size < extent_orig) { // for 32-bit systems
		return wuerr(wu_int_overflow, "uncompressed extent and control "
			"section sizes exceed size_t range");
	}

	const struct wuptr zext = mp_avail(&mp, extent_comp);
	const struct wuptr zctrl = mp_avail(&mp, ctrl_comp);
	if (!zctrl.len) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	uint8_t *buf = malloc(uncomp_size);
	if (!buf) {
		return WUERR_HERE(wu_alloc_error);
	}

	uint8_t *extent = buf;
	extent_orig = decomp_deflate(extent, extent_orig, zext.ptr, zext.len);
	uint8_t *ctrl = buf + extent_orig;
	ctrl_orig = decomp_deflate(ctrl, ctrl_orig, zctrl.ptr, zctrl.len);

	const size_t img_size = wuimg_size(img);
	uint8_t *planes = decode_zrle(extent, extent_orig, ctrl, ctrl_orig,
		img_size);
	if (planes != buf) {
		free(buf);
	}
	if (!planes) {
		return WUERR_HERE(wu_alloc_error);
	}

	/* Not sure what's supposed to happen if the image
	 * size is not a multiple of 4. */
	plane_mix((uint32_t *)img->data, planes, img_size/4);
	decode_delta(img, img_size);
	free(planes);
	return WU_OK;
}

struct wu_st hg3_parse_image(struct hg3_desc *desc, struct wuimg *img) {
	/* stdinfo tag structure
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
	const uint8_t *stdinfo = mp_slice(&desc->image, STDINFO_LEN);
	const uint8_t name[8] = {'s', 't', 'd', 'i', 'n', 'f', 'o', 0};
	if (!stdinfo) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(stdinfo, name, sizeof(name))) {
		return wuerr(wu_invalid_header, "expected stdinfo tag");
	} else if (buf_endian32l(stdinfo + 8) != STDINFO_LEN) {
		return wuerr(wu_invalid_header, "unexpected stdinfo size");
	}

	img->w = buf_endian32l(stdinfo + 16);
	img->h = buf_endian32l(stdinfo + 20);
	const uint32_t depth = buf_endian32l(stdinfo + 24);
	switch (depth) {
	case 24: case 32:
		img->channels = (uint8_t)(depth / 8);
		break;
	default: return wuerr(wu_invalid_header, "depth is neither 24 or 32");
	}
	img->bitdepth = 8;
	img->layout = pix_bgra;
	img->mirror = true;
	img->alpha = buf_endian32l(stdinfo + 44)
		? alpha_unassociated : alpha_ignore;

	desc->x = (int32_t)buf_endian32l(stdinfo + 28);
	desc->y = (int32_t)buf_endian32l(stdinfo + 32);
	desc->canvas_w = buf_endian32l(stdinfo + 36);
	desc->canvas_h = buf_endian32l(stdinfo + 40);
	return WU_OK;
}

struct wu_st hg3_next_image(struct hg3_desc *desc) {
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
	*/
	const uint8_t *entry_header = mp_slice(&desc->mp, 8);
	if (!entry_header) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const size_t next = buf_endian32l(entry_header);
	const size_t len = (next >= 8)
		? next - 8
		: desc->mp.len - desc->mp.pos;
	desc->image = mp_wuptr(mp_avail(&desc->mp, len));
	return WU_OK;
}

struct wu_st hg3_open(struct hg3_desc *desc, const struct wuptr mem) {
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
	const uint8_t id[4] = {'H', 'G', '-', '3'};
	const uint8_t *header = mp_slice(&desc->mp, 12);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(header, id, sizeof(id))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	const uint32_t size = buf_endian32l(header + 4);
	const uint32_t version = buf_endian32l(header + 8);
	return (size == 0x0c && version == 0x300)
		? WU_OK
		: wuerr(wu_invalid_header, "bad header size or version");
}
