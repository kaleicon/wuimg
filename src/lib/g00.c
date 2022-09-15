#include <stdlib.h>
#include <string.h>

#include "common/endian.h"
#include "common/math.h"
#include "raster/compost.h"
#include "g00.h"

struct g00_part_loc {
	uint32_t offset;
	uint32_t __len;
};

static const size_t G00_BLOCK_SIZE = 5*2 + 41*2;
static const size_t G00_PART_SIZE = 2*2 + 8*4 + 20*4;
static const size_t LZSS_PAD = 3 * 8;

void g00_cleanup(struct g00_desc *desc, struct raw_img *img) {
	switch (desc->version) {
	case g00_v0:
		return;
	case g00_v1:
		if (img) {
			img->data = NULL;
		}
		break;
	case g00_v2:
		;struct g00_desc_v2 *v2 = &desc->u.v2;
		free(v2->dir);
		break;
	}
	free(desc->buf);
}

static size_t lzss_decomp(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len, const size_t elem_size,
const size_t min_run) {
	size_t d = 0;
	size_t s = 0;
	while (d < dst_len && s < src_len - 1) {
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

static size_t v1_finish(struct g00_desc *desc, struct raw_img *img,
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

	struct g00_desc_v1 *v1 = &desc->u.v1;
	v1->pal_entries = buf_endian16(desc->buf, little_endian);
	if (!v1->pal_entries || v1->pal_entries > 256) {
		return 0;
	}

	const size_t pal_bytes = v1->pal_entries * 4u + 2;
	if (pal_bytes >= written) {
		return 0;
	}

	if (desc->decomp_size - pal_bytes < raw_img_size(img)) {
		return 0;
	}

	struct raster_pal *pal = raw_img_palette_init(img);
	if (!pal) {
		return 0;
	}
	memcpy(pal, desc->buf + 2, v1->pal_entries * 4);

	img->data = desc->buf + pal_bytes;
	img->borrowed = true;
	return written - pal_bytes;
}

static size_t v2_compost(struct g00_desc *desc, struct raw_img *img,
const size_t written) {
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

	img->data = calloc(1, raw_img_size(img));
	if (!img->data) {
		return 0;
	}

	const void *data_end = desc->buf + written;
	struct g00_desc_v2 *v2 = &desc->u.v2;

	struct g00_part_loc *loc = (struct g00_part_loc *)(desc->buf + 4);
	if ((void *)(loc + v2->dir_count) >= data_end
	|| buf_endian32(desc->buf, little_endian) != v2->dir_count) {
		return 0;
	}

	size_t composted = 0;
	for (uint32_t i = 0; i < v2->dir_count; ++i) {
		uint8_t *part = desc->buf + endian32(loc[i].offset, little_endian);
		if ((void *)(part + 4) >= data_end) {
			continue;
		}

		if (buf_endian16(part, little_endian) != 1) {
			continue;
		}

		const uint16_t block_count = buf_endian16(part + 2, little_endian);
		const uint8_t *block = part + G00_PART_SIZE;
		if ((void *)block >= data_end) {
			continue;
		}
		for (uint16_t b = 0; b < block_count; ++b) {
			const uint8_t *rast = block + G00_BLOCK_SIZE;
			if ((void *)rast >= data_end) {
				break;
			}
			const struct frame_info fr = {
				.x = v2->dir[i].xstart + buf_endian16(block, little_endian),
				.y = v2->dir[i].ystart + buf_endian16(block + 2, little_endian),
				.w = buf_endian16(block + 6, little_endian),
				.h = buf_endian16(block + 8, little_endian),
			};
			if (!compost_bounds_check(img->w, img->h, &fr)) {
				continue;
			}

			block = rast + fr.w * fr.h * 4;
			if ((void *)block > data_end) { // Incomplete raster
				break;
			}

			compost_overwrite(img->data, img->w, 4, rast, &fr);
			++composted;
		}
	}
	return composted;
}

size_t g00_decode(struct g00_desc *desc, struct raw_img *img) {
	desc->buf = malloc(desc->decomp_size);
	if (!desc->buf) {
		return 0;
	}

	void *src = malloc(desc->comp_size + LZSS_PAD);
	if (!src) {
		return 0;
	}

	const size_t read = fread(src, 1, desc->comp_size, desc->ifp);
	size_t written = 0;
	if (read) {
		const size_t elem_size = (desc->version == g00_v0) ? 3 : 1;
		const size_t min_run = (desc->version == g00_v0) ? 1 : 2;
		written = lzss_decomp(desc->buf, desc->decomp_size, src, read,
			elem_size, min_run);
	}
	free(src);

	if (written) {
		switch (desc->version) {
		case g00_v0:
			img->data = desc->buf;
			desc->buf = NULL;
			break;
		case g00_v1: return v1_finish(desc, img, written);
		case g00_v2: return v2_compost(desc, img, written);
		}
	}
	return written;
}

static enum wu_error header_set(struct g00_desc *desc, struct raw_img *img,
const enum g00_version version, const uint16_t width, const uint16_t height) {
	uint8_t ch;
	switch (version) {
	case g00_v0: ch = 3; break;
	case g00_v1: ch = 1; break;
	case g00_v2: ch = 4; break;
	default: return wu_unsupported_feature;
	}

	desc->version = version;
	img->w = width;
	img->h = height;
	img->channels = ch;
	img->bitdepth = 8;
	img->layout = pix_bgra;
	return raw_img_verify(img);
}

enum wu_error g00_read_header(struct g00_desc *desc, struct raw_img *img,
FILE *ifp) {
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

	 * Compressed data header, after all previous fields::
		Offset  Type    Name
		0       u32     CompressedSize   // Includes itself
		4       u32     DecompressedSize
		8

	 * Afterwards comes the compressed data.
	 * For version 0, this is 8-bit BGR pixel data.
	 * For version 1, this is an 8-bit BGRA palette followed by the
	 *     paletted raster.
	 * For version 2, this is a series of 8-bit BGRA pieces to be
	 *     composited.
	*/

	desc->ifp = ifp;
	unsigned char header[8];
	if (!fread(header, 5, 1, ifp)) {
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
		if (!fread(header, 4, 1, ifp)) {
			return wu_unexpected_eof;
		}

		struct g00_desc_v2 *v2 = &desc->u.v2;
		v2->dir_count = buf_endian32(header, little_endian);
		const size_t overflow = SIZE_MAX / dims;
		if (!v2->dir_count || v2->dir_count >= zumin(dims, overflow)) {
			return wu_invalid_header;
		}

		const size_t table_len = v2->dir_count * sizeof(*v2->dir);
		v2->dir = malloc(table_len);
		if (!v2->dir) {
			return wu_invalid_header;
		}

		if (!fread(v2->dir, table_len, 1, desc->ifp)) {
			return wu_unexpected_eof;
		}
		endian_loop32((uint32_t *)v2->dir, little_endian,
			v2->dir_count * sizeof(*v2->dir) / 4);
	}

	if (!fread(header, 8, 1, ifp)) {
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
