#include <stdlib.h>

#include <zlib.h>

#include "misc/bit.h"
#include "raster/fmt.h"
#include "hg3.h"

static const size_t STDINFO_LEN = 56;

static uint32_t biject(const uint32_t val) {
	const uint32_t repl = 0x01010101;
	return ((val & ~repl) >> 1) ^ ((val & repl) * 0xff);
}

static void plane_mix(uint32_t *data, const uint8_t *plane[static restrict 4],
const size_t plane_len) {
	/* The recipe is
	 *  1) Read a byte from the four planes
	 *  2) Split each into 4 2-bit groups
	 *  3) Place each group into a byte, highest group in the highest byte,
	 *     first planes into highest bits. Example:

		Plane0: 11001011
		Result: 11000000 00000000 10000000 11000000
		-       Byte 3   Byte 2   Byte 1   Byte 0

		Plane1: 01110101
		Result: 11010000 00110000 10010000 11010000
		-       Byte 3   Byte 2   Byte 1   Byte 0

	 *  4) For each resulting byte, if it's odd, do (255 - byte/2), else,
	 *     byte/2.
	 *  5) Write in little-endian order to the output buffer. */

	uint32_t mult, repl;
	if (which_end() == little_endian) {
		mult = (1 << 18) | (1 << 6);
		repl = 0x03030303;

		for (size_t pos = 0; pos < plane_len; ++pos) {
			uint32_t val = 0;
			for (uint8_t z = 0; z < 4; ++z) {
				uint32_t tmp = plane[z][pos] * mult;
				tmp |= tmp >> 6;
				val |= (tmp & repl) << (6 - z*2);
			}
			data[pos] = biject(val);
		}
	} else {
		mult = (1 << 30) | (1 << 20) | (1 << 10) | (1 << 0);
		repl = 0xc0c0c0c0;

		for (size_t pos = 0; pos < plane_len; ++pos) {
			uint32_t val = 0;
			for (uint8_t z = 0; z < 4; ++z) {
				val |= ((plane[z][pos] * mult) & repl) >> (z*2);
			}
			data[pos] = biject(val);
		}
	}
}

static size_t decode_delta(struct wuimg *img, const uint8_t *restrict src,
const size_t src_len) {
	size_t plane_len = src_len / 4;
	const uint8_t *plane[4] = {
		src,
		src + plane_len,
		src + plane_len*2,
		src + plane_len*3,
	};

	const size_t max = wuimg_size(img) / 4;
	if (plane_len > max) {
		plane_len = max;
	}

	plane_mix((uint32_t *)img->data, plane, plane_len);

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
	return plane_len;
}

static uint8_t * decode_data(uint8_t *restrict ext, const size_t ext_len,
uint8_t *restrict ctrl, size_t ctrl_len, size_t *data_len) {
	struct bitstrm bs = bitstrm_from_bytes(ctrl, ctrl_len);

	bool copy = bitstrm_lsb_next(&bs);
	*data_len = bitstrm_lsb_gamma(&bs, 1);
	uint8_t *data = calloc(*data_len, 1);
	if (data) {
		size_t e = 0;
		size_t d = 0;
		while (bs.pos < bs.len) {
			const size_t size = bitstrm_lsb_gamma(&bs, 1);
			if (copy) {
				if (d + size > *data_len || e + size > ext_len) {
					break;
				}
				memcpy(data + d, ext + e, size);
				e += size;
			}
			d += size;
			copy = !copy;
		}
	}
	free(ext);
	return data;
}

size_t hg3_decode(const struct hg3_desc *desc, struct wuimg *img) {
	/* img0000 header (after id string):
		Offset  Size    Name
		0       u32     ???[4]
		16      u32     ExtentCompSize
		20      u32     ExtentOrigSize
		24      u32     CtrlCompSize
		28      u32     CtrlOrigSize
		32      u8      ExtentDeflateStream[CompSize]
		--      u8      ctrlDeflateStream[CompSize]
	*/

	struct mp_parser mp = desc->image;
	const uint8_t *chunk = mp_next_slice(&mp, 32);
	if (!chunk) {
		return 0;
	}

	uLong extent_comp = buf_endian32(chunk + 16, little_endian);
	uLong extent_orig = buf_endian32(chunk + 20, little_endian);
	uLong ctrl_comp = buf_endian32(chunk + 24, little_endian);
	uLong ctrl_orig = buf_endian32(chunk + 28, little_endian);

	const struct wuptr zext = mp_next_remaining(&mp, extent_comp);
	const struct wuptr zctrl = mp_next_remaining(&mp, ctrl_comp);
	if (!zctrl.len) {
		return 0;
	}

	size_t uncomp_size = extent_orig + ctrl_orig;
	uint8_t *buf = malloc((size_t)uncomp_size + 4);
	if (!buf) {
		return 0;
	}

	uint8_t *ext = buf;
	uncompress(ext, &extent_orig, zext.ptr, zext.len);
	uint8_t *ctrl = buf + extent_orig;
	uncompress(ctrl, &ctrl_orig, zctrl.ptr, zctrl.len);
	uncomp_size = extent_orig + ctrl_orig;
	const uint8_t disrupt_gamma = 0xaa; // 10101010
	memset(buf + uncomp_size, disrupt_gamma, 4);

	size_t data_len;
	buf = decode_data(ext, extent_orig, ctrl, ctrl_orig, &data_len);

	size_t w = 0;
	if (buf) {
		if (wuimg_alloc_noverify(img)) {
			w = decode_delta(img, buf, data_len);
		}
		free(buf);
	}
	return w;
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
	img->layout = pix_bgra;
	img->mirror = true;

	desc->x = (int32_t)buf_endian32(stdinfo + 28, little_endian);
	desc->y = (int32_t)buf_endian32(stdinfo + 32, little_endian);
	desc->canvas_w = buf_endian32(stdinfo + 36, little_endian);
	desc->canvas_h = buf_endian32(stdinfo + 40, little_endian);

	const uint8_t *data = mp_next_slice(&desc->image, 8);
	if (!data) {
		return wu_unexpected_eof;
	}

	const uint8_t id[8] = "img0000\0";
	const uint8_t other[4] = "img_";
	if (!memcmp(data, id, sizeof(id))) {
		return wuimg_verify(img);
	} else if (!memcmp(data, other, sizeof(other))) {
		return wu_unsupported_feature;
	}
	return wu_invalid_header;
}

enum wu_error hg3_next_image(struct hg3_desc *desc) {
	/* ImageEntry structure
		Offset  Size    Name
		0       u32     Size     // If 0, to the end of file, else -= 8
		4       u32     ???
		8       struct  Chunks[]

	 * stdinfo chunk structure
		0       char    Name[8] // "stdinfo\0"
		8       u32     ChunkSize
		12      u32     ???
		16      u32     Width
		20      u32     Height
		24      u32     Depth
		28      i32     XOffset
		32      i32     YOffset
		36      u32     CanvasWidth
		40      u32     CanvasHeight
		44      u32     ???[3]
		56
	*/
	const uint8_t *entry_header = mp_next_slice(&desc->mp, 8);
	if (!entry_header) {
		return wu_unexpected_eof;
	}
	size_t size = buf_endian32(entry_header, little_endian);
	if (size < 8) {
		size = desc->mp.len - desc->mp.pos;
	} else {
		size -= 8;
	}

	const struct wuptr m = mp_next_remaining(&desc->mp, size);
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
		 · stdinfo chunk
		 · Compressed image chunk
		ImageEntry[...]

	 * HG-3 header:
		Offset  Size    Name
		0       u8      Identifier[4] // "HG-3"
		4       u32     HeaderSize    // 12
		8       u32     ???
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
			return (buf_endian32(header, little_endian) == 0x0c)
				? wu_ok : wu_invalid_header;
		}
		return wu_unexpected_eof;
	}
	return st;
}
