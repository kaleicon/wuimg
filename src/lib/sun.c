#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "raster/file.h"
#include "raster/fmt.h"
#include "sun.h"

// Strangely faster than memccpy
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
	const size_t file_size = (size_t)file_remaining(desc->ifp);

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

size_t sun_decode(const struct sun_desc *desc, struct raw_img *img) {
	if (raw_img_alloc_noverify(img)) {
		const size_t dst_len = raw_img_size(img);
		if (desc->type == sun_byte_encoded) {
			return rle_decode(desc, img->data, dst_len);
		}
		return fread(img->data, 1, dst_len, desc->ifp);
	}
	return 0;
}

static enum wu_error interleave_colormap(struct sun_desc *desc,
struct raw_img *img) {
	struct raster_pal *map;
	const size_t entries = 1 << img->bitdepth;
	const enum wu_error st = fmt_load_pal_planar(desc->ifp, &map,
		fmt_pal_rgb, entries);
	if (st == wu_ok) {
		raw_img_set_palette(img, map);
	}
	return st;
}

static enum wu_error validate_header(struct sun_desc *desc, struct raw_img *img,
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
	case sun_experimental:
		return wu_unsupported_feature;
	default:
		return wu_invalid_header;
	}

	switch (cm_type) {
	case sun_no_colormap:
		if (cm_len) {
			return wu_invalid_header;
		}
		break;
	case sun_rgb_colormap:
		if (bitdepth > 8 || cm_len != (1U << bitdepth) * 3) {
			return wu_invalid_header;
		}
		break;
	case sun_raw_colormap:
		return wu_unsupported_feature;
	default:
		return wu_invalid_header;
	}

	switch (bitdepth) {
	case 1: case 4:
		if (!cm_type) {
			img->attr = pix_inverted;
		}
		break;
	case 8:
		break;
	case 24:
		img->layout = (type == sun_rgb) ? pix_rgba : pix_bgra;
		break;
	case 32:
		img->layout = (type == sun_rgb) ? pix_argb : pix_abgr;
		break;
	default:
		return wu_invalid_header;
	}

	img->w = width;
	img->h = height;
	img->channels = (uint8_t)((bitdepth > 8) ? bitdepth / 8 : 1);
	img->bitdepth = (uint8_t)((bitdepth > 8) ? 8 : bitdepth);
	img->alignment = 2;

	desc->type = type;
	desc->colormap_type = cm_type;
	return raw_img_verify(img);
}

enum wu_error sun_parse_header(struct sun_desc *desc, struct raw_img *img) {
	/* SUN header (after magic bytes)
		Offset  Size    Name
		0       DWORD   Width;
		4       DWORD   Height;
		8       DWORD   Depth;          // Bits per pixel[1]
		12      DWORD   Length;         // Size of image data.
		16      DWORD   Type;           // Type of raster file
		20      DWORD   ColorMapType;
		24      DWORD   ColorMapLen;    // In bytes
		28      VAR     ColorMap;       // Planar RGB
		??
	*/

	uint32_t header[7];
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	enum wu_error st = validate_header(desc, img,
		endian32(header[0], big_endian),
		endian32(header[1], big_endian),
		endian32(header[2], big_endian),
		endian32(header[4], big_endian),
		endian32(header[5], big_endian),
		endian32(header[6], big_endian));
	if (st == wu_ok && desc->colormap_type != sun_no_colormap) {
		return interleave_colormap(desc, img);
	}
	return st;
}

enum wu_error sun_open_file(struct sun_desc *desc, FILE *ifp) {
	desc->ifp = ifp;
	const unsigned char sig[] = {0x59, 0xa6, 0x6a, 0x95};
	return fmt_sigcmp(sig, sizeof(sig), ifp);
}
