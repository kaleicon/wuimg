#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../common.h"
#include "common/composite.h"
#include "common/unpack.h"
#include "sgi.h"

struct rle_info {
	size_t tab_len;
	uint32_t *buf;
	uint32_t *row_offset;
	uint32_t *row_len;
	void *rle;
};

static void interleave_endian16(uint16_t *restrict out,
const uint16_t *restrict src, const size_t planes, const size_t plane_len) {
	if (planes == 3) {
		for (size_t i = 0; i < plane_len; ++i) {
			for (size_t ch = 0; ch < planes; ++ch) {
				out[i*planes + ch] = endian16(
					src[i + ch*plane_len], big_endian);
			}
		}
	} else if (planes == 4) {
		for (size_t i = 0; i < plane_len; ++i) {
			for (size_t ch = 0; ch < planes; ++ch) {
				out[i*planes + ch] = endian16(
					src[i + ch*plane_len], big_endian);
			}
		}
	}
}

static unsigned char * interleave_planes(const struct sgi_desc *desc,
const size_t plane_len, void *restrict planes) {
	if (desc->ch == 1) {
		return planes;
	}

	void *output = malloc(plane_len * desc->ch * desc->bytedepth);
	if (output) {
		if (desc->bytedepth == 1) {
			strip_interleave(output, planes, desc->w * desc->h,
				desc->ch, desc->bytedepth * 8);
		} else {
			interleave_endian16(output, planes, desc->ch,
				plane_len);
		}
	}
	free(planes);
	return output;
}

static unsigned char * uncompressed_decode(const struct sgi_desc *desc) {
	const size_t plane_len = desc->w * desc->h;
	const size_t dims = plane_len * desc->ch * desc->bytedepth;
	unsigned char *planes = malloc(dims);
	if (!planes) {
		return NULL;
	}

	const size_t read = fread(planes, 1, dims, desc->ifp);
	if (read != dims) {
		puts(RASTER_EOF);
	}

	return interleave_planes(desc, plane_len, planes);
}

static void rle_loop16(uint16_t *restrict output, size_t o,
const size_t out_limit, const uint16_t *restrict rle, size_t r,
const size_t rle_limit) {
	do {
		const size_t packet = endian16(rle[r], big_endian);
		const size_t len = packet & 0x7f;
		if (o + len < out_limit) {
			++r;
			if (packet & 0x80) {
				if (r + len > rle_limit) {
					break;
				}
				memcpy(output + o, rle + r, len * 2);
				r += len;
			} else {
				for (size_t i = 0; i < len; ++i) {
					output[o+i] = rle[r];
				}
				++r;
			}
			o += len;
		} else {
			break;
		}
	} while (r < rle_limit - 1);
}

static void rle_loop8(uint8_t *restrict output, size_t o,
const size_t out_limit, const uint8_t *restrict rle, size_t r,
const size_t rle_limit) {
	do {
		const size_t packet = rle[r];
		size_t len = packet & 0x7f;
		if (o + len < out_limit) {
			++r;
			if (packet & 0x80) {
				if (r + len > rle_limit) {
					break;
				}
				memcpy(output + o, rle + r, len);
				r += len;
			} else {
				memset(output + o, rle[r], len);
				++r;
			}
			o += len;
		} else {
			break;
		}
	} while (r < rle_limit - 1);
}

static void rle_loop(void *restrict output, const struct rle_info *rle,
size_t width, const size_t bytedepth) {
	for (size_t i = 0; i < rle->tab_len; ++i) {
		const size_t rle_limit = rle->row_offset[i] + rle->row_len[i];
		if (bytedepth == 1) {
			rle_loop8(output, width * i, width * (i+1),
				rle->rle, rle->row_offset[i], rle_limit);
		} else {
			rle_loop16(output, width * i, width * (i+1),
				rle->rle, rle->row_offset[i], rle_limit);
		}
	}
}

static bool resolve_offsets(struct rle_info *rle, const uint32_t max_len,
const uint32_t bytedepth) {
	const uint32_t file_pos = (uint32_t)(
		rle->tab_len * sizeof(uint32_t) * 2 + 512);
	const uint32_t min_len = bytedepth * 2;

	for (size_t i = 0; i < rle->tab_len; ++i) {
		const uint32_t offset = rle->row_offset[i] - file_pos;
		const uint32_t len = rle->row_len[i];
		if (offset + len > max_len) {
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
	struct rle_info rle = {
		.tab_len = desc->h * desc->ch,
	};

	const size_t tab_bytes = rle.tab_len * sizeof(uint32_t) * 2;
	const size_t rle_total = tab_bytes + desc->rle_size;
	rle.buf = malloc(rle_total);
	if (!rle.buf) {
		return NULL;
	}

	size_t read = fread(rle.buf, 1, rle_total, desc->ifp);
	if (read <= tab_bytes) {
		free(rle.buf);
		return NULL;
	} else if (read < rle_total) {
		puts(RASTER_EOF);
	}

	loop_endian32(rle.buf, big_endian, rle.tab_len * 2);

	rle.row_offset = rle.buf;
	rle.row_len = rle.buf + rle.tab_len;
	rle.rle = rle.buf + rle.tab_len * 2;

	const bool valid = resolve_offsets(&rle, (uint32_t)desc->rle_size,
		desc->bytedepth);
	if (!valid) {
		free(rle.buf);
		puts("SGI Error: RLE data goes out of bounds.");
		return NULL;
	}

	const size_t plane_len = desc->w * desc->h;
	const size_t dims = plane_len * desc->ch * desc->bytedepth;
	uint8_t *restrict planes = malloc(dims);
	if (!planes) {
		free(rle.buf);
		return NULL;
	}

	rle_loop(planes, &rle, desc->w, desc->bytedepth);
	free(rle.buf);
	return interleave_planes(desc, plane_len, planes);
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
	const size_t size = (size_t)file_get_remaining(desc->ifp);
	const size_t dims = desc->w * desc->h * desc->ch * desc->bytedepth;
	if (desc->compression == sgi_rle) {
		const size_t table_size = desc->h * desc->ch
			* sizeof(uint32_t) * 2;
		if (size <= table_size) {
			return lib_unexpected_eof;
		}
		// E.g. (bytedepth == 1) 01 ff  01 fe ...
		// E.g. (bytedepth == 2) 00 01 ff fe  00 01 fd fc ...
		const size_t pathological_rle = dims * 2;
		desc->rle_size = zumin(pathological_rle, size - table_size);
	} else {
		if (size < dims) {
			return lib_unexpected_eof;
		}
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
		if (channels != 1) {
			return lib_invalid_header;
		}
		break;
	case sgi_colormap:
	case sgi_colormap_define:
		return lib_sgi_is_colormap_file;
	default:
		return lib_invalid_header;
	}

	desc->w = width;
	desc->h = height;
	desc->ch = (unsigned char)channels;
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

enum lib_fail sgi_open_file(FILE *ifp, struct sgi_desc *desc) {
	unsigned char magic[2];
	if (fread(magic, 1, sizeof(magic), ifp) == sizeof(magic)) {
		if (!memcmp(magic, "\x01\xda", sizeof(magic))) {
			desc->ifp = ifp;
			return lib_ok;
		}
		return lib_invalid_signature;
	}
	return lib_unexpected_eof;
}
