// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/endian.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/compost.h"
#include "g00.h"

struct g00_part_loc {
	uint32_t offset;
	uint32_t len;
};

static const size_t G00_DIR_SIZE = 4*6;
static const size_t G00_BLOCK_SIZE = 5*2 + 41*2;
static const size_t G00_PART_SIZE = 2*2 + 8*4 + 20*4;

void g00_cleanup(struct g00_desc *desc, struct wuimg *img) {
	if (desc->version == g00_v1) {
		free(desc->u.buf);
		img->data = NULL;
	}
}

#define ENDSECTION (1 + 3*8)
static size_t lzss_decomp(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, size_t src_len, const size_t elem_size,
const size_t min_run) {
	uint8_t alt[ENDSECTION*2];
	size_t d = 0;
	size_t s = 0;
	while (d < dst_len) {
		if (s + ENDSECTION > src_len) {
			if (src == alt) {
				break;
			}
			src = mem_bufswitch(src, &s, &src_len, alt,
				sizeof(alt));
		}
		uint8_t flags = src[s];
		++s;
		for (int i = 0; i < 8; ++i, flags >>= 1) {
			if (flags & 1) {
				if (d + elem_size > dst_len) {
					return d;
				}
				memcpy(dst + d, src + s, elem_size);
				d += elem_size;
				s += elem_size;
			} else {
				const uint16_t dt = buf_endian16(src + s, little_endian);
				s += 2;

				const size_t len = ((dt & 0x0f) + min_run) * elem_size;
				const size_t off = (dt >> 4) * elem_size;
				if (off > d || dst_len - d < len) {
					return d;
				}
				for (size_t j = 0; j < len; ++j) {
					dst[d] = dst[d - off];
					++d;
				}
			}
		}
	}
	return d;
}

static size_t v1_finish(struct g00_desc *desc, struct wuimg *img,
const size_t written) {
	/* V1 decoded data format:
		Offset  Size    Name
		0       WORD    NrEntries
		2       BYTE[4] PaletteEntries[NrEntries]
		*       BYTE    PixelData
	*/
	if (written < 2) {
		return 0;
	}

	const uint16_t pal_entries = buf_endian16(desc->u.buf, little_endian);
	if (!pal_entries || pal_entries > 256) {
		return 0;
	}

	const size_t pal_bytes = pal_entries * 4u + 2;
	if (pal_bytes >= written) {
		return 0;
	}

	if (desc->decomp_size - pal_bytes < wuimg_size(img)) {
		return 0;
	}

	struct palette *pal = img->u.palette;
	memcpy(pal->color, desc->u.buf + 2, pal_entries * 4);

	img->data = desc->u.buf + pal_bytes;
	img->borrowed = true;
	return written - pal_bytes;
}

static size_t v2_compost(struct g00_desc *desc, struct wuimg *img,
const size_t written, const uint8_t *buf) {
	/* V2 decoded data format:
		Offset  Type    Name
		0       u32     NrParts
		4       struct  PartLocation[NrParts]

	 * PartLocation struct:
		Offset  Type    Name
		0       u32     Offset // Relative to the decoded data start
		4       u32     Size
		8

	 * Part struct:
		Offset  Type    Name
		0       u16     Type              // [1]
		2       u16     BlockCount
		4       u32     XHotspot
		8       u32     YHotspot
		12      u32     Width
		16      u32     Height
		20      u32     XScreen
		24      u32     YScreen
		28      u32     FullPartWidth
		32      u32     FullPartHeight
		36      u32     Reserved[20]
		116     struct  Block[BlockCount]

	 * Block struct:
		Offset  Type    Name
		0       u16     BlockX
		2       u16     BlockY
		4       u16     Info
		6       u16     BlockWidth
		8       u16     BlockHeight
		10      u16     Reserved[41]
		92      u8      Raster[]

	 * [1] All images I've tested have a value of 1, but apparently 0 and 2
	 *     are also possible. I have no idea what the field means anyway.[2]
	 * [2] I have no idea what most of the fields are used for, actually.
	*/

	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}

	const void *data_end = buf + written;
	struct g00_desc_v2 *v2 = &desc->u.v2;

	struct g00_part_loc *loc = (struct g00_part_loc *)(buf + 4);
	if ((void *)(loc + v2->dir_count) >= data_end
	|| buf_endian32(buf, little_endian) != v2->dir_count) {
		return 0;
	}

	size_t composted = 0;
	for (uint32_t i = 0; i < v2->dir_count; ++i) {
		const uint8_t *part = buf + endian32(loc[i].offset, little_endian);
		if ((void *)(part + 4) >= data_end) {
			continue;
		}

		if (buf_endian16(part, little_endian) != 1) {
			continue;
		}

		const uint16_t block_count = buf_endian16(part + 2, little_endian);
		const uint8_t *block = part + G00_PART_SIZE;
		if ((const void *)block >= data_end) {
			continue;
		}
		const uint32_t xstart = buf_endian32(v2->dir + i*G00_DIR_SIZE, little_endian);
		const uint32_t ystart = buf_endian32(v2->dir + i*G00_DIR_SIZE + 4, little_endian);
		for (uint16_t b = 0; b < block_count; ++b) {
			const uint8_t *rast = block + G00_BLOCK_SIZE;
			if ((const void *)rast >= data_end) {
				break;
			}
			const struct compost reg = {
				.x = xstart + buf_endian16(block, little_endian),
				.y = ystart + buf_endian16(block + 2, little_endian),
				.w = buf_endian16(block + 6, little_endian),
				.h = buf_endian16(block + 8, little_endian),
			};
			if (!compost_bounds_check(img->w, img->h, &reg)) {
				continue;
			}

			block = rast + reg.w * reg.h * 4;
			if ((const void *)block > data_end) { // Incomplete raster
				break;
			}

			compost_overwrite(img->data, img->w, 4, rast, &reg);
			++composted;
		}
	}
	return composted;
}

enum wu_error g00_decode(struct g00_desc *desc, struct wuimg *img) {
	void *dst = malloc(desc->decomp_size);
	if (dst) {
		const struct wuptr src = mp_avail(&desc->mp, desc->comp_size);
		const size_t elem_size = (desc->version == g00_v0) ? 3 : 1;
		const size_t min_run = (desc->version == g00_v0) ? 1 : 2;
		size_t written = lzss_decomp(dst, desc->decomp_size,
			src.ptr, src.len, elem_size, min_run);
		switch (desc->version) {
		case g00_v0:
			img->data = dst;
			break;
		case g00_v1:
			desc->u.buf = dst;
			written = v1_finish(desc, img, written);
			break;
		case g00_v2:
			written = v2_compost(desc, img, written, dst);
			free(dst);
			break;
		}
		return written ? wu_ok : wu_decoding_error;
	}
	return wu_alloc_error;
}

static enum wu_error header_set(struct g00_desc *desc, struct wuimg *img,
const enum g00_version version, const uint16_t width, const uint16_t height) {
	uint8_t ch;
	switch (version) {
	case g00_v0: ch = 3; break;
	case g00_v1:
		if (!wuimg_palette_init(img)) {
			return wu_alloc_error;
		}
		ch = 1;
		break;
	case g00_v2: ch = 4; break;
	default: return wu_unsupported_feature;
	}

	desc->version = version;
	img->w = width;
	img->h = height;
	img->channels = ch;
	img->bitdepth = 8;
	img->layout = pix_bgra;
	return wuimg_verify(img);
}

enum wu_error g00_parse(struct g00_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* Base header:
		Offset  Type    Name
		0       u8      Version // 0, 1, or 2
		1       u16     Width
		3       u16     Height
		5

	 * Extra fields for v2, after base:
		Offset  Type    Name
		0       u32     Count
		4       struct  Regions[Count]

			0       u32     XStart
			4       u32     YStart
			8       u32     XEnd
			12      u32     Xend
			16      u32     Reserved[2]
			24

	 * Compressed data header, after all previous fields:
		Offset  Type    Name
		0       u32     CompressedSize   // Includes itself
		4       u32     DecompressedSize
		8

	 * Afterwards comes the compressed data.
	 * For version 0, this is 8-bit BGR pixel data.
	 * For version 1, this is an 8-bit BGRA palette followed by the
	 *     raster.
	 * For version 2, this is a series of 8-bit BGRA pieces to be
	 *     composited.
	*/

	*desc = (struct g00_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t *header = mp_slice(&desc->mp, 5);
	if (!header) {
		return wu_unexpected_eof;
	}

	enum wu_error st = header_set(desc, img, header[0],
		buf_endian16(header + 1, little_endian),
		buf_endian16(header + 3, little_endian));
	if (st != wu_ok) {
		return st;
	}

	const size_t dims = img->w * img->h;
	if (desc->version == g00_v2) {
		header = mp_slice(&desc->mp, 4);
		if (!header) {
			return wu_unexpected_eof;
		}

		struct g00_desc_v2 *v2 = &desc->u.v2;
		v2->dir_count = buf_endian32(header, little_endian);
		if (!v2->dir_count || v2->dir_count >= zumin(dims, 0xffff)) {
			return wu_invalid_header;
		}

		const size_t table_len = v2->dir_count * G00_DIR_SIZE;
		desc->u.v2.dir = mp_slice(&desc->mp, table_len);
		if (!desc->u.v2.dir) {
			return wu_unexpected_eof;
		}
	}

	header = mp_slice(&desc->mp, 8);
	if (!header) {
		return wu_unexpected_eof;
	}
	desc->comp_size = buf_endian32(header, little_endian);
	if (desc->comp_size <= 8) {
		return wu_invalid_header;
	}
	desc->comp_size -= 8;

	if (desc->version == g00_v2) {
		desc->decomp_size = buf_endian32(header + 4, little_endian);
	} else {
		desc->decomp_size = dims * img->channels;
		if (desc->version == g00_v1) {
			desc->decomp_size += 2 + 4*256;
		}
	}
	return wu_ok;
}
