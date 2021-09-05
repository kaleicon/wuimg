#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common.h"
#include "../raster/unpack.h"
#include "sgi.h"

static const uint8_t RLE_LEN_MASK = 0x7f;

struct rle_info {
	size_t entries;
	uint32_t *buf;
	uint32_t *row_offset;
	uint32_t *row_len;
	void *rle;
};

static bool undec16(uint16_t *restrict dst, uint16_t *restrict src,
const struct sgi_desc *desc) {
	const size_t row_size = desc->rast.w * desc->rast.ch;
	for (size_t z = 0; z < desc->rast.ch; ++z) {
		for (size_t y = 0; y < desc->rast.h; ++y) {
			const size_t read = fread(src, 2, desc->rast.w, desc->ifp);
			for (size_t x = 0; x < read; ++x) {
				dst[y*row_size + x*desc->rast.ch + z] =
					endian16(src[x], big_endian);
			}
			if (read < desc->rast.w) {
				return false;
			}
		}
	}
	return true;
}

static bool undec8(uint8_t *restrict dst, uint8_t *restrict src,
const struct sgi_desc *desc) {
	const size_t row_size = desc->rast.w * desc->rast.ch;
	for (size_t z = 0; z < desc->rast.ch; ++z) {
		for (size_t y = 0; y < desc->rast.h; ++y) {
			const size_t read = fread(src, 1, desc->rast.w, desc->ifp);
			strip_spread(dst + y*row_size + z, src, read, desc->rast.ch);
			if (read < desc->rast.w) {
				return false;
			}
		}
	}
	return true;
}

static unsigned char * uncompressed_decode(const struct sgi_desc *desc) {
	const size_t dims = raster_size(&desc->rast);
	void *output = malloc(dims);
	if (!output) {
		return NULL;
	}

	const size_t row_len = desc->rast.w * desc->bytedepth;
	void *row = malloc(row_len);
	if (!row) {
		free(output);
		return NULL;
	}

	bool full;
	if (desc->bytedepth == 2) {
		full = undec16(output, row, desc);
	} else {
		full = undec8(output, row, desc);
	}
	free(row);
	if (!full) {
		puts(RASTER_EOF);
	}
	return output;
}

static void rle_loop16(uint16_t *restrict output, const size_t out_limit,
const uint16_t *restrict rle, const uint32_t rle_limit, const uint8_t ch) {
	size_t o = 0;
	uint32_t r = 0;
	do {
		const uint16_t packet = endian16(rle[r], big_endian);
		const uint16_t len = packet & RLE_LEN_MASK;
		++r;
		if (packet & 0x80) {
			for (uint16_t i = 0; i < len; ++i) {
				output[o*ch] = endian16(rle[r], big_endian);
				++o;
				++r;
			}
		} else {
			for (uint16_t i = 0; i < len; ++i) {
				output[o*ch] = endian16(rle[r], big_endian);
				++o;
			}
			++r;
		}
	} while (r < rle_limit - 1 && o < out_limit);
}

static void rle_loop8(uint8_t *restrict output, const size_t out_limit,
const uint8_t *restrict rle, const uint32_t rle_limit, const uint8_t ch) {
	size_t o = 0;
	uint32_t r = 0;
	do {
		const uint8_t packet = rle[r];
		const uint8_t len = packet & RLE_LEN_MASK;
		++r;
		if (packet & 0x80) {
			for (uint8_t i = 0; i < len; ++i) {
				output[o*ch] = rle[r];
				++o;
				++r;
			}
		} else {
			for (uint8_t i = 0; i < len; ++i) {
				output[o*ch] = rle[r];
				++o;
			}
			++r;
		}
	} while (r < rle_limit - 1 && o < out_limit);
}

static void rle_loop(void *restrict output, const struct rle_info *rle,
const struct sgi_desc *desc) {
	const size_t width = desc->rast.w;
	const size_t height = desc->rast.h;
	const size_t stride = width * desc->rast.ch;
	for (uint8_t plane = 0; plane < desc->rast.ch; ++plane) {
		for (size_t y = 0; y < height; ++y) {
			const size_t offset = stride*y + plane;
			const uint32_t row_off = rle->row_offset[plane*height + y];
			const uint32_t row_len = rle->row_len[plane*height + y];
			if (desc->bytedepth == 1) {
				rle_loop8((uint8_t *)output + offset, width,
					(uint8_t *)rle->rle + row_off, row_len,
					desc->rast.ch);
			} else {
				rle_loop16((uint16_t *)output + offset, width,
					(uint16_t *)rle->rle + row_off, row_len,
					desc->rast.ch);
			}
		}
	}
}

static bool resolve_offsets(struct rle_info *rle, const uint32_t rle_len,
const uint32_t bytedepth) {
	const uint32_t file_pos = (uint32_t)(
		rle->entries * sizeof(uint32_t) * 2 + 512);
	const uint32_t min_len = bytedepth * 2;

	for (size_t i = 0; i < rle->entries; ++i) {
		const uint32_t offset = endian32(rle->row_offset[i], big_endian) - file_pos;
		const uint32_t len = endian32(rle->row_len[i], big_endian);
		if (offset > rle_len || len > rle_len) {
			return false;
		} else if (len < min_len) {
			return false;
		} else if (offset % bytedepth || len % bytedepth) {
			return false;
		}
		rle->row_offset[i] = offset/bytedepth;
		rle->row_len[i] = len/bytedepth;
	}
	return true;
}

static unsigned char * rle_decode(const struct sgi_desc *desc) {
	/* RLE table:
		LONG    RLEOffset[Y*Z]; // From the beginning of the file. In bytes
		LONG    RLELen[Y*Z];    // In bytes
	*/
	struct rle_info rle = {
		.entries = desc->rast.h * desc->rast.ch,
	};

	const size_t table_size = rle.entries * sizeof(uint32_t) * 2;
	const size_t rle_total = table_size + desc->rle_size;
	rle.buf = malloc(rle_total + RLE_LEN_MASK * desc->bytedepth);
	if (!rle.buf) {
		return NULL;
	}

	size_t read = fread(rle.buf, 1, rle_total, desc->ifp);
	if (read <= table_size) {
		free(rle.buf);
		return NULL;
	} else if (read < rle_total) {
		puts(RASTER_EOF);
	}

	rle.row_offset = rle.buf;
	rle.row_len = rle.buf + rle.entries;
	rle.rle = rle.buf + rle.entries * 2;

	const bool valid = resolve_offsets(&rle, (uint32_t)desc->rle_size,
		desc->bytedepth);
	if (!valid) {
		free(rle.buf);
		puts("SGI Error: RLE data goes out of bounds.");
		return NULL;
	}

	const size_t plane_len = desc->rast.w * desc->rast.h + RLE_LEN_MASK;
	void *output = malloc(plane_len * desc->rast.ch * desc->bytedepth);
	if (!output) {
		free(rle.buf);
		return NULL;
	}

	rle_loop(output, &rle, desc);
	free(rle.buf);
	return output;
}

unsigned char * sgi_decode(const struct sgi_desc *desc) {
	switch (desc->compression) {
	case sgi_uncompressed:
		return uncompressed_decode(desc);
	case sgi_rle:
		return rle_decode(desc);
	}
	return NULL;
}


static enum lib_fail validate_filesize(struct sgi_desc *desc) {
	if (desc->compression == sgi_rle) {
		const size_t size = (size_t)file_get_remaining(desc->ifp);
		const size_t dims = desc->rast.w * desc->rast.h
			* desc->rast.ch * desc->bytedepth;
		const size_t table_size = desc->rast.h * desc->rast.ch
			* sizeof(uint32_t) * 2;
		if (size <= table_size) {
			return lib_unexpected_eof;
		}
		// E.g. (bytedepth == 1) 01 ff  01 ff ...
		// E.g. (bytedepth == 2) 00 01 ff ff  00 01 ff ff ...
		const size_t pathological_rle = dims * 2;
		desc->rle_size = zumin(pathological_rle, size - table_size);
	}
	return lib_ok;
}

static enum lib_fail validate_header(struct sgi_desc *desc,
const uint8_t compression, const uint8_t bytedepth,
const uint16_t dimension, const uint16_t width, const uint16_t height,
const uint16_t channels, const uint32_t bitmap_type) {
	switch (compression) {
	case sgi_uncompressed: case sgi_rle:
		break;
	default:
		return lib_invalid_header;
	}

	switch (bytedepth) {
	case 1: case 2:
		break;
	default:
		return lib_invalid_header;
	}

	switch (dimension) {
	case 1:
		if (height != 1) {
			return lib_invalid_header;
		}
		// fallthrough
	case 2:
		if (channels != 1) {
			return lib_invalid_header;
		}
		break;
	case 3:
		switch (channels) {
		case 1: case 3: case 4:
			break;
		default:
			return lib_invalid_header;
		}
		break;
	default:
		return lib_invalid_header;
	}

	if (!width || !height) {
		return lib_invalid_header;
	}

	switch (bitmap_type) {
	case sgi_raw:
		break;
	case sgi_332:
		if (channels != 1 || bytedepth != 1) {
			return lib_invalid_header;
		}
		break;
	case sgi_colormap:
	case sgi_colormap_define:
		return lib_sgi_is_colormap_file;
	default:
		return lib_invalid_header;
	}

	desc->rast = (struct raster_desc) {
		.w = width,
		.h = height,
		.ch = (unsigned char)channels,
		.bitdepth = bytedepth * 8,
		.attr = (bitmap_type == sgi_332) ? pix_packing_332 : 0,
	};
	raster_normalize(&desc->rast);
	desc->bytedepth = bytedepth;
	desc->compression = (enum sgi_compression)compression;
	desc->type = (enum sgi_bitmap_type)bitmap_type;
	return lib_ok;
}

enum lib_fail sgi_parse_header(struct sgi_desc *desc) {
	/* SGI header (after magic bytes)
		Offset  Size    Name
		0       CHAR    Compression;
		1       CHAR    BytesPerPixel;
		2       WORD    Dimension;
		4       WORD    XSize;
		6       WORD    YSize;
		8       WORD    ZSize;
		10      LONG    PixMin;
		14      LONG    PixMax;
		18      CHAR    Dummy1[4];
		22      CHAR    ImageName[80];
		102     LONG    ColorMap;       // Bitmap interpretation
		106     CHAR    Dummy2[404];
		510
	*/
	uint8_t buf[14];
	if (fread(buf, 1, 10, desc->ifp) != 10) {
		return lib_unexpected_eof;
	}

	fseek(desc->ifp, 12, SEEK_CUR);
	const size_t name_len = sizeof(desc->name);
	if (fread(desc->name, 1, name_len, desc->ifp) != name_len) {
		return lib_unexpected_eof;
	}

	if (fread(buf + 10, 1, 4, desc->ifp) != 4) {
		return lib_unexpected_eof;
	}

	const enum lib_fail fail = validate_header(desc,
		buf[0], buf[1],
		buf_endian16(buf + 2, big_endian),
		buf_endian16(buf + 4, big_endian),
		buf_endian16(buf + 6, big_endian),
		buf_endian16(buf + 8, big_endian),
		buf_endian32(buf + 10, big_endian));
	if (fail) {
		return fail;
	}

	fseek(desc->ifp, 512, SEEK_SET);
	return validate_filesize(desc);
}

enum lib_fail sgi_open_file(struct sgi_desc *desc, FILE *ifp) {
	const unsigned char sig[2] = {0x01, 0xda};
	unsigned char magic[sizeof(sig)];
	if (fread(magic, 1, sizeof(magic), ifp) == sizeof(magic)) {
		if (!memcmp(magic, sig, sizeof(magic))) {
			desc->ifp = ifp;
			return lib_ok;
		}
		return lib_invalid_signature;
	}
	return lib_unexpected_eof;
}
