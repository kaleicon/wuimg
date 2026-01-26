// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc/math.h"
#include "raster/fmt.h"
#include "sgi.h"

static const unsigned HEADER_SIZE = 512;
static const uint8_t RLE_LEN_MASK = 0x7f;

const char * sgi_compression_str(const enum sgi_compression c) {
	switch (c) {
	case sgi_uncompressed: return "None";
	case sgi_rle: return "RLE";
	}
	return "???";
}

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

struct wu_st sgi_decode(const struct sgi_desc *desc, struct wuimg *img) {
	size_t w;
	if (desc->compression == sgi_rle) {
		w = rle_decode(desc, img);
	} else {
		fseek(desc->ifp, (long)HEADER_SIZE, SEEK_SET);
		w = fmt_load_raster_swap(img, desc->ifp, big_endian);
	}
	return wuerr_partial(w, wuimg_size(img));
}

struct wu_st sgi_parse_header(struct sgi_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* SGI header
		Offset  Size    Name
		0       CHAR    Signature[2];
		2       CHAR    Compression;
		3       CHAR    BytesPerPixel;
		4       WORD    Dimension;
		6       WORD    XSize;
		8       WORD    YSize;
		10      WORD    ZSize;
		12      LONG    PixMin;        // Min value
		16      LONG    PixMax;        // Max value
		20      CHAR    Dummy1[4];
		24      CHAR    ImageName[80];
		104     LONG    ColorMap;      // Bitmap interpretation
		108     CHAR    Dummy2[404];
		512
	*/
	const unsigned char sig[2] = {0x01, 0xda};
	unsigned char buf[12];
	if (!fread(buf, sizeof(buf), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(buf, sig, sizeof(sig))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	const unsigned char compression = buf[2];
	const unsigned char bytedepth = buf[3];
	if (bytedepth < 1 || bytedepth > 2) {
		return wuerr(wu_invalid_header, "bad bytes per pixels");
	}
	const uint16_t dimension = buf_endian16b(buf + 4);
	const uint16_t width = buf_endian16b(buf + 6);
	const uint16_t height = buf_endian16b(buf + 8);
	const uint16_t channels = buf_endian16b(buf + 10);
	switch (dimension) {
	case 1:
		if (height != 1) {
			return wuerr(wu_invalid_header,
				"height != 1 in 1D file");
		}
		// fallthrough
	case 2:
		if (channels != 1) {
			return wuerr(wu_invalid_header,
				"channels != 1 in 2D file");
		}
		break;
	case 3:
		switch (channels) {
		case 1: case 3: case 4:
			break;
		default:
			return wuerr(wu_invalid_header, "bad nb of channels");
		}
		break;
	default:
		return wuerr(wu_invalid_header, "bad nb of dimensions");
	}

	fseek(ifp, 12, SEEK_CUR);
	if (!fread(desc->name, sizeof(desc->name), 1, ifp)) {
		return wuerr(wu_unexpected_eof, "EOF while reading name field");
	}

	uint32_t bitmap_type;
	if (!fread(&bitmap_type, sizeof(bitmap_type), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	bitmap_type = endian32b(bitmap_type);
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
				desc->ifp = ifp;
				desc->bytedepth = bytedepth;
				desc->compression = compression;
				desc->type = (enum sgi_bitmap_type)bitmap_type;
				return WU_OK;
			}
			return WUERR_HERE(wu_alloc_error);
		}
		return wuerr(wu_invalid_header, "compression is not raw nor rle");
	case sgi_332:
		return wuerr(wu_samples_wanted, "332 image file");
	case sgi_colormap:
	case sgi_colormap_define:
		return wuerr(wu_samples_wanted, "colormap-only file");
	}
	return wuerr(wu_invalid_header, "bad bitmap type");
}
