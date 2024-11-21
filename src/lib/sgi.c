// SPDX-License-Identifier: 0BSD
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc/math.h"
#include "raster/fmt.h"
#include "sgi.h"

static const unsigned HEADER_SIZE = 512;
static const uint8_t RLE_LEN_MASK = 0x7f;

struct rle_info {
	size_t rows;
	size_t total;
	uint8_t *buf;
	uint32_t *row_offset;
	uint32_t *row_len;
};

static size_t rle_loop16(uint16_t *restrict output, const size_t out_limit,
const uint16_t *restrict rle, const uint32_t rle_limit) {
	size_t o = 0;
	uint32_t r = 0;
	while (rle_limit - r > 1) {
		const uint16_t packet = endian16(rle[r], big_endian);
		const uint16_t len = packet & RLE_LEN_MASK;
		if (len > out_limit - o) {
			break;
		}
		++r;
		if (packet & 0x80) {
			if (len > rle_limit - r) {
				break;
			}
			for (uint16_t i = 0; i < len; ++i) {
				output[o] = endian16(rle[r], big_endian);
				++o;
				++r;
			}
		} else {
			const uint16_t pix = endian16(rle[r], big_endian);
			for (uint16_t i = 0; i < len; ++i) {
				output[o] = pix;
				++o;
			}
			++r;
		}
	}
	return o;
}

static size_t rle_loop8(uint8_t *restrict output, const size_t out_limit,
const uint8_t *restrict rle, const uint32_t rle_limit) {
	size_t o = 0;
	uint32_t r = 0;
	while (rle_limit - r > 1) {
		const uint8_t packet = rle[r];
		const uint8_t len = packet & RLE_LEN_MASK;
		if (len > out_limit - o) {
			break;
		}
		++r;
		if (packet & 0x80) {
			if (len > rle_limit - r) {
				break;
			}
			memcpy(output + o, rle + r, len);
			r += len;
		} else {
			memset(output + o, rle[r], len);
			++r;
		}
		o += len;
	}
	return o;
}

static size_t rle_loop(const struct sgi_desc *desc, struct wuimg *img,
const struct rle_info *rle) {
	size_t w = 0;
	for (size_t i = 0; i < rle->rows; ++i) {
		uint32_t off = endian32(rle->row_offset[i], big_endian);
		uint32_t len = endian32(rle->row_len[i], big_endian);
		if (off > UINT32_MAX - len || off + len > rle->total) {
			continue;
		}

		const size_t width = img->w;
		const size_t line = width*i;
		off /= desc->bytedepth;
		len /= desc->bytedepth;
		if (desc->bytedepth == 1) {
			w += rle_loop8((uint8_t *)img->data + line, width,
				(uint8_t *)rle->buf + off, len);
		} else {
			w += rle_loop16((uint16_t *)img->data + line, width,
				(uint16_t *)rle->buf + off, len);
		}
	}
	return w;
}

static size_t get_total_size(const struct sgi_desc *desc,
const size_t non_rle, const size_t dims) {
	fseek(desc->ifp, 0, SEEK_END);
	const size_t size = (size_t)ftell(desc->ifp);
	if (size > non_rle) {
		// E.g. (bytedepth == 1) 01 ff  01 ff ...
		// E.g. (bytedepth == 2) 00 01 ff ff  00 01 ff ff ...
		const size_t pathological_rle = dims * 2;
		return zumin(pathological_rle + non_rle, size);
	}
	return 0;
}

static size_t rle_decode(const struct sgi_desc *desc, struct wuimg *img) {
	/* RLE table:
		u32     RLEOffset[Y*Z];
		u32     RLELen[Y*Z];

	 * Both fields are in bytes, and offsets are from the beginning of the
	 * file.
	*/

	struct rle_info rle;
	rle.rows = img->h * img->channels;
	const size_t table_size = rle.rows * sizeof(uint32_t) * 2;
	const size_t non_rle = HEADER_SIZE + table_size;
	rle.total = get_total_size(desc, non_rle, wuimg_size(img));

	size_t w = 0;
	if (rle.total) {
		/* Just load the whole file so we don't have to subtract
		 * offsets and all that jazz. */
		rle.buf = malloc(rle.total);

		if (rle.buf) {
			fseek(desc->ifp, 0, SEEK_SET);
			const size_t read = fread(rle.buf, 1, rle.total, desc->ifp);
			if (read > non_rle) {
				rle.row_offset = (uint32_t *)(rle.buf + HEADER_SIZE);
				rle.row_len = rle.row_offset + rle.rows;
				w = rle_loop(desc, img, &rle);
			}
			free(rle.buf);
		}
	}
	return w;
}

size_t sgi_decode(const struct sgi_desc *desc, struct wuimg *img) {
	if (wuimg_alloc_noverify(img)) {
		if (desc->compression == sgi_rle) {
			return rle_decode(desc, img);
		}
		fseek(desc->ifp, (long)HEADER_SIZE, SEEK_SET);
		return fmt_load_raster_swap(img, desc->ifp, big_endian);
	}
	return 0;
}

static enum wu_error validate_header(struct sgi_desc *desc, struct wuimg *img,
const uint8_t compression, const uint8_t bytedepth,
const uint16_t dimension, const uint16_t width, const uint16_t height,
const uint16_t channels, const uint32_t bitmap_type) {
	if (bytedepth < 1 || bytedepth > 2) {
		return wu_invalid_header;
	}

	switch (dimension) {
	case 1:
		if (height != 1) {
			return wu_invalid_header;
		}
		// fallthrough
	case 2:
		if (channels != 1) {
			return wu_invalid_header;
		}
		break;
	case 3:
		switch (channels) {
		case 1: case 3: case 4:
			break;
		default:
			return wu_invalid_header;
		}
		break;
	default:
		return wu_invalid_header;
	}

	switch (bitmap_type) {
	case sgi_raw:
		switch (compression) {
		case sgi_uncompressed: case sgi_rle:
			img->w = width;
			img->h = height;
			img->channels = (unsigned char)channels;
			img->bitdepth = bytedepth * 8;
			img->mirror = true;
			if (wuimg_plane_init(img)) {
				desc->bytedepth = bytedepth;
				desc->compression = compression;
				desc->type = (enum sgi_bitmap_type)bitmap_type;
				return wuimg_verify(img);
			}
			return wu_alloc_error;
		}
		break;
	case sgi_332:
		return wu_samples_wanted;
	case sgi_colormap:
	case sgi_colormap_define:
		return wu_no_image_data;
	}
	return wu_invalid_header;
}

enum wu_error sgi_parse_header(struct sgi_desc *desc, struct wuimg *img) {
	/* SGI header (after magic bytes)
		Offset  Size    Name
		0       CHAR    Compression;
		1       CHAR    BytesPerPixel;
		2       WORD    Dimension;
		4       WORD    XSize;
		6       WORD    YSize;
		8       WORD    ZSize;
		10      LONG    PixMin;        // Min value
		14      LONG    PixMax;        // Max value
		18      CHAR    Dummy1[4];
		22      CHAR    ImageName[80];
		102     LONG    ColorMap;      // Bitmap interpretation
		106     CHAR    Dummy2[404];
		510
	*/
	uint8_t buf[14];
	if (!fread(buf, 10, 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	fseek(desc->ifp, 12, SEEK_CUR);
	const size_t name_len = sizeof(desc->name);
	if (!fread(desc->name, name_len, 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	if (!fread(buf + 10, 4, 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	return validate_header(desc, img, buf[0], buf[1],
		buf_endian16(buf + 2, big_endian),
		buf_endian16(buf + 4, big_endian),
		buf_endian16(buf + 6, big_endian),
		buf_endian16(buf + 8, big_endian),
		buf_endian32(buf + 10, big_endian));
}

enum wu_error sgi_open_file(struct sgi_desc *desc, FILE *ifp) {
	const unsigned char sig[2] = {0x01, 0xda};
	const enum wu_error st = fmt_sigcmp(sig, sizeof(sig), ifp);
	if (st == wu_ok) {
		desc->ifp = ifp;
	}
	return st;
}
