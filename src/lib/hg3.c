// SPDX-License-Identifier: 0BSD
#include <stdlib.h>

#include <zlib.h>

#include "misc/bit.h"
#include "raster/fmt.h"
#include "hg3.h"

/* Format documentation (mostly complete):
https://github.com/trigger-segfault/TriggersTools.CatSystem2/wiki/HG%E2%80%903-Image
https://github.com/trigger-segfault/TriggersTools.CatSystem2/wiki/HG%E2%80%90X-ProcessImage
*/

static const size_t STDINFO_LEN = 56;

static uint32_t biject(const uint32_t val) {
	const uint32_t repl = 0x01010101;
	return ((val & ~repl) >> 1) ^ ((val & repl) * 0xff);
}

static void plane_mix(uint32_t *data, const uint8_t *restrict plane,
const size_t plane_len) {
	/* The recipe is
	 *  1) Read a byte from each of the four planes
	 *  2) Split each byte into 4 2-bit groups
	 *  3) Place each group into a byte, highest group in the highest byte,
	 *     first planes into highest bits. Example:

		Plane0: 11010011
		Output: 11000000 01000000 00000000 11000000
		-       Byte 3   Byte 2   Byte 1   Byte 0

		Plane1: 01110101
		Output: 11010000 01110000 00010000 11010000
		-       Byte 3   Byte 2   Byte 1   Byte 0

	 *  4) For each resulting byte, if it's odd, do (255 - byte/2), else,
	 *     byte/2.
	 *  5) Write in little-endian order to the output buffer. The result is
	 *     an image in BGR/BGRA order.
	 * Note that we use a faster algorithm here, that outputs in BGRA
	 * order on big-endian, and in ARGB order on little-endian. This is
	 * accounted for when setting the image layout in hg3_parse_image(). */

	const uint32_t mult = (1 << 30) | (1 << 20) | (1 << 10) | (1 << 0);
	const uint32_t repl = 0xc0c0c0c0;
	for (size_t pos = 0; pos < plane_len; ++pos) {
		uint32_t val = 0;
		for (uint8_t z = 0; z < 4; ++z) {
			val |= ((plane[z*plane_len + pos] * mult) & repl) >> (z*2);
		}
		data[pos] = biject(val);
	}
}

static void decode_delta(struct wuimg *img, const uint8_t *restrict src,
const size_t src_len) {
	plane_mix((uint32_t *)img->data, src, src_len/4);

	const uint8_t ch = img->channels;
	const size_t stride = img->w * ch;
	for (size_t x = ch; x < stride; ++x) {
		img->data[x] += img->data[x - ch];
	}
	for (size_t y = 1; y < img->h; ++y) {
		for (size_t x = 0; x < stride; ++x) {
			img->data[stride*y + x] += img->data[stride*(y-1) + x];
		}
	}
}

static uint8_t * decode_data(uint8_t *restrict ext, const size_t ext_len,
uint8_t *restrict ctrl, size_t ctrl_len, const size_t data_len, bool *dont_free) {
	struct bitstrm bs = bitstrm_from_bytes(ctrl, ctrl_len);

	bool copy = bitstrm_lsb_next(&bs);
	const size_t stream_len = bitstrm_lsb_gamma(&bs, 1);
	if (stream_len != data_len) {
		return NULL;
	}

	if (bs.pos >= bs.len) {
		return NULL;
	}
	size_t size = bitstrm_lsb_gamma(&bs, 1);
	if (copy && size == data_len) {
		*dont_free = true;
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
			if (bs.pos >= bs.len) {
				break;
			}
			size = bitstrm_lsb_gamma(&bs, 1);
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
	struct mp_parser mp = desc->image;
	const uint8_t *tag = mp_next_slice(&mp, 32);
	if (!tag) {
		return false;
	}

	uLong extent_comp = buf_endian32(tag + 16, little_endian);
	uLong extent_orig = buf_endian32(tag + 20, little_endian);
	uLong ctrl_comp = buf_endian32(tag + 24, little_endian);
	uLong ctrl_orig = buf_endian32(tag + 28, little_endian);

	const struct wuptr zext = mp_next_remaining(&mp, extent_comp);
	const struct wuptr zctrl = mp_next_remaining(&mp, ctrl_comp);
	if (!zctrl.len) {
		return false;
	}

	const size_t gamma_pad = 4;
	size_t uncomp_size = extent_orig + ctrl_orig;
	uint8_t *buf = malloc((size_t)uncomp_size + gamma_pad);
	if (!buf) {
		return false;
	}

	uint8_t *extent = buf;
	uncompress(extent, &extent_orig, zext.ptr, zext.len);
	uint8_t *ctrl = buf + extent_orig;
	uncompress(ctrl, &ctrl_orig, zctrl.ptr, zctrl.len);
	uncomp_size = extent_orig + ctrl_orig;
	const uint8_t disrupt_gamma = 0xaa; // 10101010
	memset(buf + uncomp_size, disrupt_gamma, gamma_pad);

	const size_t data_len = wuimg_size(img);
	bool dont_free = false;
	uint8_t *data = decode_data(extent, extent_orig, ctrl, ctrl_orig,
		data_len, &dont_free);
	if (!dont_free) {
		free(buf);
	}

	bool ok = false;
	if (data) {
		if (wuimg_alloc_noverify(img)) {
			decode_delta(img, data, data_len);
			ok = true;
		}
		free(data);
	}
	return ok;
}

enum wu_error hg3_parse_image(struct hg3_desc *desc, struct wuimg *img) {
	const uint8_t *stdinfo = mp_next_slice(&desc->image, STDINFO_LEN);
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
	img->layout = (which_end() == little_endian) ? pix_argb : pix_bgra;
	img->mirror = true;
	img->alpha = buf_endian32(stdinfo + 44, little_endian)
		? alpha_unassociated : alpha_ignore;

	desc->x = (int32_t)buf_endian32(stdinfo + 28, little_endian);
	desc->y = (int32_t)buf_endian32(stdinfo + 32, little_endian);
	desc->canvas_w = buf_endian32(stdinfo + 36, little_endian);
	desc->canvas_h = buf_endian32(stdinfo + 40, little_endian);

	const uint8_t *tag = mp_next_slice(&desc->image, 8);
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
	const uint8_t *entry_header = mp_next_slice(&desc->mp, 8);
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

	const struct wuptr m = mp_next_remaining(&desc->mp, len);
	if (m.len > STDINFO_LEN + 8) {
		desc->image = mp_parser_mem(m.len, m.ptr);
		return wu_ok;
	}
	return wu_unexpected_eof;
}

enum wu_error hg3_open(struct hg3_desc *desc, const struct mp_parser mp) {
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
		.mp = mp,
	};
	const uint8_t id[4] = "HG-3";
	const enum wu_error st = fmt_sigcmp_mem(id, sizeof(id), &desc->mp);
	if (st == wu_ok) {
		const uint8_t *header = mp_next_slice(&desc->mp, 8);
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
