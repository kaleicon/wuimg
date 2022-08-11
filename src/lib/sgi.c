#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raster/file.h"
#include "raster/fmt.h"
#include "raster/unpack.h"
#include "sgi.h"

static const uint8_t RLE_LEN_MASK = 0x7f;

struct rle_info {
	size_t entries;
	size_t table_size;
	size_t rle_size;
	size_t total;
	uint32_t *row_offset; // Base pointer
	uint32_t *row_len;
	void *rle;
};

static void rle_loop16(uint16_t *restrict output, const size_t out_limit,
const uint16_t *restrict rle, const uint32_t rle_limit) {
	size_t o = 0;
	uint32_t r = 0;
	do {
		const uint16_t packet = endian16(rle[r], big_endian);
		const uint16_t len = packet & RLE_LEN_MASK;
		if (out_limit - o < len) {
			return;
		}
		++r;
		if (packet & 0x80) {
			for (uint16_t i = 0; i < len; ++i) {
				output[o] = endian16(rle[r], big_endian);
				++o;
				++r;
			}
		} else {
			const uint16_t pix = endian16(rle[r], big_endian);
			for (uint16_t i = 0; i < len; ++i) {
				output[o + i] = pix;
			}
			o += len;
			++r;
		}
	} while (rle_limit - r >= 2);
}

static void rle_loop8(uint8_t *restrict output, const size_t out_limit,
const uint8_t *restrict rle, const uint32_t rle_limit) {
	size_t o = 0;
	uint32_t r = 0;
	do {
		const uint8_t packet = rle[r];
		const uint8_t len = packet & RLE_LEN_MASK;
		if (out_limit - o < len) {
			return;
		}
		++r;
		if (packet & 0x80) {
			memcpy(output + o, rle + r, len);
			o += len;
			r += len;
		} else {
			memset(output + o, rle[r], len);
			o += len;
			++r;
		}
	} while (rle_limit - r >= 2);
}

static void rle_loop(const struct sgi_desc *desc, struct raw_img *img,
const struct rle_info *rle) {
	void *restrict output = img->data;
	const size_t width = img->w;
	for (size_t y = 0; y < rle->entries; ++y) {
		const size_t offset = width*y;
		const uint32_t row_off = rle->row_offset[y];
		const uint32_t row_len = rle->row_len[y];
		if (desc->bytedepth == 1) {
			rle_loop8((uint8_t *)output + offset, width,
				(uint8_t *)rle->rle + row_off, row_len);
		} else {
			rle_loop16((uint16_t *)output + offset, width,
				(uint16_t *)rle->rle + row_off, row_len);
		}
	}
}

static bool resolve_offsets(struct rle_info *rle, const uint8_t bytedepth) {
	const uint32_t file_pos = (uint32_t)(512 + rle->table_size);
	const uint32_t rle_end = (uint32_t)(file_pos + rle->rle_size);
	const uint32_t min_len = bytedepth * 2; // Packet + payload

	for (size_t i = 0; i < rle->entries; ++i) {
		const uint32_t offset = endian32(rle->row_offset[i], big_endian);
		const uint32_t len = endian32(rle->row_len[i], big_endian);
		if (offset + len > rle_end) {
			return false;
		} else if (len < min_len) {
			return false;
		} else if (offset % bytedepth || len % bytedepth) {
			return false;
		}
		rle->row_offset[i] = (offset - file_pos)/bytedepth;
		rle->row_len[i] = len/bytedepth;
	}
	return true;
}

static size_t get_filesize(const struct sgi_desc *desc,
const size_t table_size, const size_t dims) {
	const size_t size = file_remaining(desc->ifp);
	if (size > table_size) {
		// E.g. (bytedepth == 1) 01 ff  01 ff ...
		// E.g. (bytedepth == 2) 00 01 ff ff  00 01 ff ff ...
		const size_t pathological_rle = dims * 2;
		return zumin(pathological_rle, size - table_size);
	}
	return 0;
}

static size_t rle_decode(const struct sgi_desc *desc, struct raw_img *img,
const size_t dims) {
	/* RLE table:
		LONG    RLEOffset[Y*Z]; // From the beginning of the file. In bytes
		LONG    RLELen[Y*Z];    // In bytes
	*/

	struct rle_info rle;
	rle.entries = img->h * img->channels;
	rle.table_size = rle.entries * sizeof(uint32_t) * 2;
	rle.rle_size = get_filesize(desc, rle.table_size, dims);

	bool ok = false;
	if (rle.rle_size) {
		const size_t padding = RLE_LEN_MASK * desc->bytedepth;
		rle.total = rle.table_size + rle.rle_size;
		rle.row_offset = malloc(rle.total + padding);

		if (rle.row_offset) {
			const size_t read = fread(rle.row_offset, 1, rle.total,
				desc->ifp);
			if (read > rle.table_size) {
				rle.row_len = rle.row_offset + rle.entries;
				rle.rle = rle.row_len + rle.entries;

				if (resolve_offsets(&rle, desc->bytedepth)) {
					rle_loop(desc, img, &rle);
					ok = true;
				}
			}
			free(rle.row_offset);
		}
	}
	return ok;
}

size_t sgi_decode(const struct sgi_desc *desc, struct raw_img *img) {
	if (raw_img_alloc(img) == wu_ok) {
		const size_t size = raw_img_size(img);
		fseek(desc->ifp, 512, SEEK_SET);
		if (desc->compression == sgi_rle) {
			return rle_decode(desc, img, size);
		}
		return fmt_load_raster(img, desc->ifp, big_endian);
	}
	return 0;
}

static enum wu_error validate_header(struct sgi_desc *desc, struct raw_img *img,
const uint8_t compression, const uint8_t bytedepth,
const uint16_t dimension, const uint16_t width, const uint16_t height,
const uint16_t channels, const uint32_t bitmap_type) {
	switch (compression) {
	case sgi_uncompressed: case sgi_rle:
		break;
	default: return wu_invalid_header;
	}

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
		break;
	case sgi_332:
		// This could easily be supported, if we had any samples.
		return wu_unsupported_feature;
	case sgi_colormap:
	case sgi_colormap_define:
		return wu_no_image_data;
	default:
		return wu_invalid_header;
	}

	img->w = width;
	img->h = height;
	img->channels = (unsigned char)channels;
	img->bitdepth = bytedepth * 8;
	img->mirror = true;
	if (raw_img_plane_init(img)) {
		desc->bytedepth = bytedepth;
		desc->compression = (enum sgi_compression)compression;
		desc->type = (enum sgi_bitmap_type)bitmap_type;
		return raw_img_verify(img);
	}
	return wu_alloc_error;
}

enum wu_error sgi_parse_header(struct sgi_desc *desc, struct raw_img *img) {
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
