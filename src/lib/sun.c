#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../common.h"
#include "sun.h"

static const size_t RLE_MAX_RUN = 256;
static const unsigned char RLE_FLAG = 0x80;

void sun_cleanup(struct sun_desc *desc) {
	raster_free(&desc->rast);
}

static size_t run_length_loop(unsigned char *restrict output,
const unsigned char *restrict rle, const size_t out_limit,
const size_t rle_limit) {
	size_t o = 0;
	size_t r = 0;
	// We've padded both buffers so we can skip some bound checks
	do {
		// This is somehow slightly faster than using memccpy
		const unsigned char *flag_pos = memchr(rle + r, RLE_FLAG,
			RLE_MAX_RUN);
		if (flag_pos) {
			if (flag_pos != rle + r) {
				const size_t read = (size_t)(flag_pos - (rle + r));
				memcpy(output + o, rle + r, read);
				o += read;
				r += read;
			}

			const unsigned char run_count = rle[r+1];
			if (run_count) {
				const unsigned char run_val = rle[r+2];
				memset(output + o, run_val, run_count + 1);
				o += run_count + 1;
				r += 3;
			} else {
				output[o] = RLE_FLAG;
				++o;
				r += 2;
			}
		} else {
			memcpy(output + o, rle + r, RLE_MAX_RUN);
			o += RLE_MAX_RUN;
			r += RLE_MAX_RUN;
		}
	} while (r < rle_limit && o < out_limit);
	return o;
}

static unsigned char * rle_decode(const struct sun_desc *desc) {
	const size_t raster_len = raster_size(&desc->rast);

	// E.g. 0x80 0x00 0x80 0x00... -> 0x80 0x80...
	const size_t pathological_rle = raster_len * 2;
	const size_t file_size = (size_t)file_get_remaining(desc->ifp);

	const size_t rle_len = zumin(file_size, pathological_rle);
	unsigned char *rle = malloc(rle_len + 2);
	if (!rle) {
		return NULL;
	}

	const size_t read = fread(rle, 1, rle_len, desc->ifp);
	rle[read] = RLE_FLAG;
	rle[read+1] = 0;

	unsigned char *output = malloc(raster_len + RLE_MAX_RUN);
	if (!output) {
		free(rle);
		return NULL;
	}

	const size_t written = run_length_loop(output, rle, raster_len, read);
	free(rle);
	if (written < raster_len - 1) { // - 1 due to alignment
		puts("SUN warning: Run-length decoding didn't fill the whole "
			"buffer. Output may contain garbage.");
	}
	return output;
}

unsigned char * sun_decode(const struct sun_desc *desc) {
	if (desc->type == sun_byte_encoded) {
		return rle_decode(desc);
	}
	struct memory mem;
	if (lib_load_rast(&mem, &desc->rast, desc->ifp)) {
		return mem.data;
	}
	return NULL;
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
	if (!width || !height) {
		return lib_invalid_header;
	}

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
	raster_normalize(&desc->rast);
	return lib_ok;
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
