#include <stdio.h>
#include <string.h>

#include "common.h"
#include "common_composite.h"
#include "lib_sgi.h"

static void interleave_planes16(const unsigned ch, const size_t plane_len,
uint16_t *restrict output, const uint16_t *restrict red) {
	const uint16_t *restrict green = red + plane_len;
	const uint16_t *restrict blue = red + plane_len * 2;
	const uint16_t *restrict alpha = red + plane_len * 3;
	if (ch == 3) {
		for (size_t i = 0; i < plane_len; ++i) {
			output[i*ch] = endian16(red[i], big_endian);
			output[i*ch + 1] = endian16(green[i], big_endian);
			output[i*ch + 2] = endian16(blue[i], big_endian);
		}
	} else if (ch == 4) {
		for (size_t i = 0; i < plane_len; ++i) {
			output[i*ch] = endian16(red[i], big_endian);
			output[i*ch + 1] = endian16(green[i], big_endian);
			output[i*ch + 2] = endian16(blue[i], big_endian);
			output[i*ch + 3] = endian16(alpha[i], big_endian);
		}
	}
}

static void interleave_planes8(const unsigned ch, const size_t plane_len,
uint8_t *restrict output, uint8_t *restrict red) {
	const uint8_t *restrict green = red + plane_len;
	const uint8_t *restrict blue = red + plane_len * 2;
	const uint8_t *restrict alpha = red + plane_len * 3;
	if (ch == 3) {
		for (size_t i = 0; i < plane_len; ++i){
			output[i*ch] = red[i];
			output[i*ch + 1] = green[i];
			output[i*ch + 2] = blue[i];
		}
	} else if (ch == 4) {
		for (size_t i = 0; i < plane_len; ++i) {
			output[i*ch] = red[i];
			output[i*ch + 1] = green[i];
			output[i*ch + 2] = blue[i];
			output[i*ch + 3] = alpha[i];
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
			interleave_planes8(desc->ch, plane_len, output, planes);
		} else {
			interleave_planes16(desc->ch, plane_len, output, planes);
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

static void rle_loop16(uint16_t *restrict output,
const uint16_t *restrict out_limit, const uint16_t *restrict rle,
const uint16_t *restrict rle_limit) {
	do {
		const unsigned packet = endian16(*rle, big_endian);
		const unsigned len = packet & 0x7f;
		if (len && output + len <= out_limit) {
			++rle;
			if (packet & 0x80) {
				if (rle + len > rle_limit) {
					break;
				}
				memcpy(output, rle, len * 2);
				rle += len;
			} else {
				color_set(output, rle, len, 2);
				++rle;
			}
			output += len;
		} else {
			break;
		}
	} while (rle + 1 < rle_limit);
}

static void rle_loop8(uint8_t *restrict output,
const uint8_t *restrict out_limit, const uint8_t *restrict rle,
const uint8_t *restrict rle_limit) {
	do {
		const unsigned packet = *rle;
		const unsigned len = packet & 0x7f;
		if (len && output + len <= out_limit) {
			++rle;
			if (packet & 0x80) {
				if (rle + len > rle_limit) {
					break;
				}
				memcpy(output, rle, len);
				rle += len;
			} else {
				memset(output, *rle, len);
				++rle;
			}
			output += len;
		} else {
			break;
		}
	} while (rle + 1 < rle_limit);
}

static void rle_loop(const unsigned bytedepth, uint8_t *restrict planes,
const uint32_t *restrict rle_offset, const uint32_t *restrict rle_rowlen,
const uint8_t *restrict rle_data, size_t width, const size_t tab_len) {
	width *= bytedepth;
	for (size_t i = 0; i < tab_len; ++i) {
		void *restrict output = planes + width * i;
		const void *restrict out_limit = planes + width * (i+1);

		const void *restrict rle_row = rle_data + rle_offset[i];
		const void *restrict rle_limit = rle_data
			+ rle_offset[i] + rle_rowlen[i];

		if (bytedepth == 1) {
			rle_loop8(output, out_limit, rle_row, rle_limit);
		} else {
			rle_loop16(output, out_limit, rle_row, rle_limit);
		}
	}
}

static bool resolve_offsets(uint32_t *restrict rle_offset,
const uint32_t *restrict rle_rowlen, const size_t tab_len,
const uint32_t max_len, const unsigned bytedepth) {
	const size_t file_pos = tab_len * sizeof(uint32_t) * 2 + 512;
	const unsigned min_len = bytedepth * 2;

	unsigned align = bytedepth - 1;
	for (size_t i = 0; i < tab_len; ++i) {
		rle_offset[i] -= (uint32_t)file_pos;
		if (rle_rowlen[i] < min_len || rle_rowlen[i] > max_len) {
			return false;
		} else if (rle_offset[i] > max_len - rle_rowlen[i]) {
			return false;
		} else if (rle_offset[i] & align || rle_rowlen[i] & align) {
			return false;
		}
	}
	return true;
}

static unsigned char * rle_decode(const struct sgi_desc *desc) {
	const size_t tab_len = desc->h * desc->ch;
	const size_t tab_bytes = tab_len * sizeof(uint32_t) * 2;
	const size_t rle_total = tab_bytes + desc->rle_size;
	uint32_t *restrict rle = malloc(rle_total);
	if (!rle) {
		return NULL;
	}

	size_t read = fread(rle, 1, rle_total, desc->ifp);
	if (read <= tab_bytes) {
		free(rle);
		return NULL;
	} else if (read < rle_total) {
		puts(RASTER_EOF);
	}

	loop_endian32(rle, big_endian, tab_len * 2);

	uint32_t *restrict rle_offset = rle;
	uint32_t *restrict rle_rowlen = rle + tab_len;
	const bool valid = resolve_offsets(rle_offset, rle_rowlen, tab_len,
		(uint32_t)desc->rle_size, desc->bytedepth);
	if (!valid) {
		free(rle);
		puts("SGI Error: RLE data goes out of bounds.");
		return NULL;
	}

	uint8_t *restrict rle_data = (uint8_t *)(rle + tab_len * 2);

	const size_t plane_len = desc->w * desc->h;
	const size_t dims = plane_len * desc->ch * desc->bytedepth;
	uint8_t *restrict planes = malloc(dims);
	if (!planes) {
		free(rle);
		return NULL;
	}

	rle_loop(desc->bytedepth, planes, rle_offset, rle_rowlen, rle_data,
		desc->w, tab_len);
	free(rle);
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
	const long start = ftell(desc->ifp);
	fseek(desc->ifp, 0, SEEK_END);
	const long end = ftell(desc->ifp);

	const size_t dims = desc->w * desc->h * desc->ch * desc->bytedepth;
	const size_t size = (size_t)(end - start);
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
	fseek(desc->ifp, start, SEEK_SET);
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
		// Fallthrough
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
