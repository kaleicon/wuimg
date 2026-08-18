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
static const size_t G00_PART_LOC_SIZE = 8;

void g00_cleanup(struct g00_desc *desc, struct wuimg *img) {
	if (desc->version == g00_v1) {
		free(desc->u.buf);
		img->data = NULL;
	}
}

#define ENDSECTION (1 + 3*8)
static size_t g00_lzss_decomp(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, size_t src_len, const size_t elem_size,
const size_t min_run) {
	uint8_t alt[ENDSECTION*2];
	size_t d = 0;
	size_t s = 0;
	while (d < dst_len) {
		if (src_len - s < ENDSECTION) {
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
				if (dst_len - d < elem_size) {
					return d;
				}
				memcpy(dst + d, src + s, elem_size);
				d += elem_size;
				s += elem_size;
			} else {
				const uint16_t dt = buf_endian16l(src + s);
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

static size_t g00_v1_finish(struct g00_desc *desc, struct wuimg *img,
const size_t written) {
	/* V1 decoded data format:
		Offset  Type    Name
		0       WORD    NrEntries
		2       BYTE    PaletteEntries[NrEntries][4]
		*       BYTE    PixelData
	*/
	if (written < 2) {
		return 0;
	}

	const uint16_t pal_entries = buf_endian16l(desc->u.buf);
	if (!pal_entries || pal_entries > 256) {
		return 0;
	}

	const size_t pal_bytes = pal_entries * 4u + 2;
	if (pal_bytes >= written) {
		return 0;
	}

	struct palette *pal = img->u.palette;
	memcpy(pal->color, desc->u.buf + 2, pal_entries * 4);

	img->data = desc->u.buf + pal_bytes;
	img->borrowed = true;
	return written - pal_bytes;
}

static struct wu_st g00_v2_compost(struct g00_desc *desc, struct wuimg *img,
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

	const size_t part_loc_off = 4;
	struct g00_desc_v2 *v2 = &desc->u.v2;
	struct mparser mp = mp_mem(written, buf);
	const uint8_t *loc = mp_slice(&mp,
		part_loc_off + G00_PART_LOC_SIZE*v2->dir_count);
	if (!loc) {
		return wuerr(wu_unexpected_eof, "not enough data for dir table");
	} else if (buf_endian32l(loc) != v2->dir_count) {
		return wuerr(wu_invalid_header, "g00 v2: dir_count mismatch");
	}

	if (!wuimg_alloc_noverify(img)) {
		return WUERR_HERE(wu_alloc_error);
	}

	size_t total = v2->dir_count;
	size_t composted = 0;
	for (uint32_t i = 0; i < v2->dir_count; ++i) {
		mp_seek_set(&mp, buf_endian32l(loc + part_loc_off + G00_PART_LOC_SIZE*i));
		const uint8_t *part = mp_slice(&mp, G00_PART_SIZE);
		if (!part) {
			continue;
		}

		const uint16_t block_count = buf_endian16l(part + 2);
		++composted;
		total += block_count;
		if (false && buf_endian16l(part) != 1) {
			continue;
		}

		const uint32_t xstart = buf_endian32l(v2->dir + i*G00_DIR_SIZE);
		const uint32_t ystart = buf_endian32l(v2->dir + i*G00_DIR_SIZE + 4);
		for (uint16_t b = 0; b < block_count; ++b) {
			const uint8_t *block = mp_slice(&mp, G00_BLOCK_SIZE);
			if (!block) {
				break;
			}
			const struct compost reg = {
				.x = xstart + buf_endian16l(block),
				.y = ystart + buf_endian16l(block + 2),
				.w = buf_endian16l(block + 6),
				.h = buf_endian16l(block + 8),
			};
			if (!compost_bounds_check(&reg, img)) {
				break;
			}
			const uint8_t *rast = mp_slice(&mp, reg.w * reg.h * 4);
			if (!rast) {
				break;
			}

			compost_overwrite(&reg, img, rast);
			++composted;
		}
	}
	return wuerr_partial(composted, total);
}

struct wu_st g00_decode(struct g00_desc *desc, struct wuimg *img) {
	uint8_t *dst = calloc(desc->decomp_size, 1);
	if (dst) {
		const struct wuptr src = mp_avail(&desc->mp, desc->comp_size);
		const size_t elem_size = (desc->version == g00_v0) ? 3 : 1;
		const size_t min_run = (desc->version == g00_v0) ? 1 : 2;
		size_t written = g00_lzss_decomp(dst, desc->decomp_size,
			src.ptr, src.len, elem_size, min_run);
		switch (desc->version) {
		case g00_v0:
			img->data = dst;
			break;
		case g00_v1:
			desc->u.buf = dst;
			written = g00_v1_finish(desc, img, written);
			break;
		case g00_v2:
			;struct wu_st st = g00_v2_compost(desc, img, written,
				dst);
			free(dst);
			return st;
		}
		return wuerr_partial(written, wuimg_size(img));
	}
	return WUERR_HERE(wu_alloc_error);
}

struct wu_st g00_parse(struct g00_desc *desc, struct wuimg *img,
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
		return WUERR_HERE(wu_unexpected_eof);
	}

	img->w = buf_endian16l(header + 1);
	img->h = buf_endian16l(header + 3);
	img->bitdepth = 8;
	img->layout = pix_bgra;

	const size_t dims = img->w * img->h;
	desc->version = header[0];
	switch (desc->version) {
	case g00_v0:
		img->channels = 3;
		break;
	case g00_v1:
		if (!wuimg_palette_init(img)) {
			return WUERR_HERE(wu_alloc_error);
		}
		img->channels = 1;
		break;
	case g00_v2:
		img->channels = 4;

		header = mp_slice(&desc->mp, 4);
		if (!header) {
			return WUERR_HERE(wu_unexpected_eof);
		}

		struct g00_desc_v2 *v2 = &desc->u.v2;
		v2->dir_count = buf_endian32l(header);
		if (!v2->dir_count || v2->dir_count >= zumin(dims, 0xffff)) {
			return wuerr(wu_invalid_header,
				"g00 v2: dir_count out of bounds");
		}

		const size_t table_len = v2->dir_count * G00_DIR_SIZE;
		desc->u.v2.dir = mp_slice(&desc->mp, table_len);
		if (!desc->u.v2.dir) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		break;
	default:
		return wuerr(wu_invalid_header, "g00 version > 2");
	}

	header = mp_slice(&desc->mp, 8);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	desc->comp_size = buf_endian32l(header);
	if (desc->comp_size <= 8) {
		return WUERR_HERE(wu_invalid_header);
	}
	desc->comp_size -= 8;
	desc->decomp_size = buf_endian32l(header + 4);
	if (desc->version != g00_v2) {
		size_t max = dims * img->channels;
		if (desc->version == g00_v1) {
			max += 2 + 4*256;
		}
		desc->decomp_size = (uint32_t)zumin(desc->decomp_size, max);
	}
	return wuimg_verify_st(img);
}
