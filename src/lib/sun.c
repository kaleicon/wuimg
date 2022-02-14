#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../common.h"
#include "sun.h"

void sun_cleanup(struct sun_desc *desc) {
	raster_free(&desc->rast);
}

// Strangely faster than memccpy
__attribute__((unused))
static size_t memccpy_cur(uint8_t *restrict dst,
const uint8_t *restrict src, const uint8_t c, size_t dst_len, size_t src_len) {
	const uint8_t *end = memchr(src, c, src_len);
	if (end) {
		src_len = (size_t)(end - src);
	}
	if (src_len > dst_len) {
		src_len = dst_len;
	}
	memcpy(dst, src, src_len);
	return src_len;
}

static size_t run_length_loop(unsigned char *restrict dst, const size_t dst_len,
const unsigned char *restrict rle, const size_t rle_len) {
	const unsigned char RLE_FLAG = 0x80;
	size_t d = 0;
	size_t r = 0;
	while (d < dst_len && rle_len - r >= 2) {
		while (rle[r] == RLE_FLAG) {
			const unsigned char run_count = rle[r+1];
			if (run_count) {
				if (dst_len - d < (size_t)run_count + 1
				|| rle_len - r < 3) {
					return d;
				}
				memset(dst + d, rle[r+2], run_count + 1);
				d += run_count + 1;
				r += 3;
			} else {
				dst[d] = RLE_FLAG;
				++d;
				r += 2;
			}
			if (d >= dst_len && rle_len - r < 2) {
				return d;
			}
		}

		const size_t read = memccpy_cur(dst + d, rle + r, RLE_FLAG,
			dst_len - d, rle_len - r);
		d += read;
		r += read;
	}
	return d;
}

static size_t rle_decode(const struct sun_desc *desc,
unsigned char *restrict dst, const size_t dst_len) {
	// E.g. 0x80 0x00 0x80 0x00... -> 0x80 0x80...
	const size_t pathological_rle = dst_len * 2;
	const size_t file_size = (size_t)file_get_remaining(desc->ifp);

	size_t written = 0;
	const size_t rle_len = zumin(file_size, pathological_rle);
	unsigned char *rle = malloc(rle_len);
	if (rle) {
		const size_t read = fread(rle, 1, rle_len, desc->ifp);
		written = run_length_loop(dst, dst_len, rle, read);
		free(rle);
	}
	return written;
}

size_t sun_decode(const struct sun_desc *desc, void *restrict dst) {
	const size_t dst_len = raster_size(&desc->rast);
	if (desc->type == sun_byte_encoded) {
		return rle_decode(desc, dst, dst_len);
	}
	return fread(dst, 1, dst_len, desc->ifp);
}

static enum lib_fail interleave_colormap(struct sun_desc *desc) {
	/* Colormap is stored per plane in RGB order */
	struct raster_pal *map = malloc(sizeof(*map));
	if (!map) {
		return lib_alloc_error;
	}
	desc->rast.palette = map;

	const size_t entries = 1 << desc->rast.bitdepth;
	const size_t len = entries * 3;
	unsigned char *buf = malloc(len);
	if (!buf) {
		return lib_alloc_error;
	}

	enum lib_fail status = lib_unexpected_eof;
	if (fread(buf, len, 1, desc->ifp)) {
		for (size_t i = 0; i < entries; ++i) {
			map->color[i].r = buf[i];
			map->color[i].g = buf[i + entries];
			map->color[i].b = buf[i + entries * 2];
			map->color[i].a = 0xff;
		}
		status = lib_ok;
	}
	free(buf);
	return status;
}

static enum lib_fail validate_header(struct sun_desc *desc,
const uint32_t width, const uint32_t height, const uint32_t bitdepth,
const uint32_t type, const uint32_t cm_type, const uint32_t cm_len) {
	switch (type) {
	case sun_old:
	case sun_standard:
	case sun_byte_encoded:
	case sun_rgb:
		break;
	case sun_tiff:
	case sun_iff:
		return lib_sun_unsupported_type;
	case sun_experimental:
		return lib_sun_experimental_type;
	default:
		return lib_invalid_header;
	}

	switch (cm_type) {
	case sun_no_colormap:
		if (cm_len) {
			return lib_invalid_header;
		}
		break;
	case sun_rgb_colormap:
		if (bitdepth > 8 || cm_len != (1U << bitdepth) * 3) {
			return lib_invalid_header;
		}
		break;
	case sun_raw_colormap:
		return lib_sun_uses_raw_colormap;
	default:
		return lib_invalid_header;
	}

	enum pix_layout layout = 0;
	switch (bitdepth) {
	case 1: case 4: case 8:
		break;
	case 24:
		layout = (type == sun_rgb) ? pix_rgba : pix_bgra;
		break;
	case 32:
		layout = (type == sun_rgb) ? pix_argb : pix_abgr;
		break;
	default:
		return lib_invalid_header;
	}

	desc->rast = (struct raster_desc) {
		.w = width,
		.h = height,
		.ch = (unsigned char)((bitdepth > 8) ? bitdepth / 8 : 1),
		.bitdepth = (unsigned char)((bitdepth > 8) ? 8 : bitdepth),
		.alignment = 2,
		.layout = layout,
		.attr = (bitdepth < 8 && !cm_type) ? pix_inverted : pix_normal,
	};
	desc->type = type;
	desc->colormap_type = cm_type;
	return lib_ok;
}

enum lib_fail sun_parse_header(struct sun_desc *desc) {
	/* SUN header (after magic bytes)
		Offset  Size    Name
		0       DWORD   Width;
		4       DWORD   Height;
		8       DWORD   Depth;          // Bits per pixel
		12      DWORD   Length;         // Size of image data. Unreliable
		16      DWORD   Type;           // Type of raster file
		20      DWORD   ColorMapType;
		24      DWORD   ColorMapLen;    // In bytes
		28      VAR     ColorMap;
		??
	*/

	uint32_t header[7];
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	enum lib_fail fail = validate_header(desc,
		endian32(header[0], big_endian),
		endian32(header[1], big_endian),
		endian32(header[2], big_endian),
		endian32(header[4], big_endian),
		endian32(header[5], big_endian),
		endian32(header[6], big_endian));
	if (fail) {
		return fail;
	}

	if (desc->colormap_type != sun_no_colormap) {
		fail = interleave_colormap(desc);
		if (fail != lib_ok) {
			return fail;
		}
	}
	return raster_normalize(&desc->rast) ? lib_ok : lib_int_overflow;
}

enum lib_fail sun_open_file(struct sun_desc *desc, FILE *ifp) {
	const unsigned char sig[] = {0x59, 0xa6, 0x6a, 0x95};
	const enum lib_fail st = lib_sigcmp(sig, sizeof(sig), ifp);
	if (st == lib_ok) {
		desc->ifp = ifp;
		desc->rast.palette = NULL;
	}
	return st;
}
