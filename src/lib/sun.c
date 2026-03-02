// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <string.h>

#include "misc/file.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "sun.h"

const char * sun_type_str(const enum sun_type t) {
	switch (t) {
	case sun_old: return "Old";
	case sun_standard: return "Standard";
	case sun_byte_encoded: return "Byte encoded";
	case sun_rgb: return "RGB";
	case sun_tiff: return "TIFF";
	case sun_iff: return "IFF";
	case sun_experimental: return "Experimental";
	}
	return "???";
}

static struct wu_st sun_rle_decode(unsigned char *restrict dst,
const size_t dst_len, FILE *ifp) {
	const unsigned char RLE_FLAG = 0x80;
	uint8_t rle[BUFSIZ];
	const size_t RLE_MAX = 3;
	memset(rle, 0, RLE_MAX); // in case of eof at the start
	size_t d = 0;
	size_t r = 0;
	size_t rle_len = 0;
	while (d < dst_len) {
		if (rle_len - r < RLE_MAX) {
			rle_len -= r;
			memcpy(rle, rle + r, rle_len);
			rle_len += fread(rle + rle_len, 1, sizeof(rle) - rle_len, ifp);
			if (!rle_len) {
				break;
			}
			r = 0;
		}
		uint8_t c = rle[r];
		if (c == RLE_FLAG) {
			++r;
			size_t run_count = rle[r];
			++r;
			if (run_count) {
				c = rle[r];
				++r;
			}
			++run_count;
			if (dst_len - d < run_count) {
				break;
			}
			memset(dst + d, c, run_count);
			d += run_count;
		} else {
			const size_t read = memccpy_cur(dst + d, rle + r,
				RLE_FLAG, dst_len - d, rle_len - r);
			d += read;
			r += read;
		}
	}
	return wuerr_partial(d, dst_len);
}

struct wu_st sun_decode(const struct sun_desc *desc, struct wuimg *img) {
	return (desc->type == sun_byte_encoded)
		? sun_rle_decode(img->data, wuimg_size(img), desc->ifp)
		: fmt_load_raster_st(img, desc->ifp);
}

static struct wu_st sun_interleave_colormap(struct sun_desc *desc,
struct wuimg *img) {
	uint8_t buf[256*3];
	if (fread(buf, sizeof(buf), 1, desc->ifp)) {
		struct palette *map = wuimg_palette_init(img);
		if (map) {
			const size_t entries = 1 << img->bitdepth;
			for (size_t i = 0; i < entries; ++i) {
				map->color[i] = (struct pix_rgba8) {
					.r = buf[i],
					.g = buf[i + entries],
					.b = buf[i + entries*2],
					.a = 0xff,
				};
			}
			return WU_OK;
		}
		return WUERR_HERE(wu_alloc_error);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

struct wu_st sun_parse_header(struct sun_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* SUN header
		Offset  Size    Name
		0       BYTE    Signature[4];
		4       DWORD   Width;
		8       DWORD   Height;
		12      DWORD   Depth;          // Bits per pixel[1]
		16      DWORD   Length;         // Size of image data.
		20      DWORD   Type;           // Type of raster file
		24      DWORD   ColorMapType;
		28      DWORD   ColorMapLen;    // In bytes
		32      VAR     ColorMap;       // Planar RGB
		??
	*/

	const unsigned char sig[] = {0x59, 0xa6, 0x6a, 0x95};
	uint32_t header[8];
	if (!fread(header, sizeof(header), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(header, sig, sizeof(sig))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	const uint32_t type = endian32b(header[5]);
	switch (type) {
	case sun_old:
	case sun_standard:
	case sun_byte_encoded:
	case sun_rgb:
		break;
	case sun_tiff:
	case sun_iff:
	case sun_experimental:
		return wuerr(wu_samples_wanted,
			"TIFF, IFF, or Experimental encoding");
	default:
		return wuerr(wu_invalid_header, "unknown raster encoding");
	}

	const uint32_t cm_type = endian32b(header[6]);
	const uint32_t cm_len = endian32b(header[7]);
	const uint32_t bitdepth = endian32b(header[3]);
	switch (cm_type) {
	case sun_no_colormap:
		if (cm_len) {
			return wuerr(wu_invalid_header,
				"colormap size != 0");
		}
		break;
	case sun_rgb_colormap:
		if (bitdepth > 8) {
			return wuerr(wu_invalid_header,
				"bitdepth > 8 in colormapped file");
		} else if (cm_len != (3u << bitdepth)) {
			return wuerr(wu_invalid_header,
				"bitdepth and colormap size mismatch");
		}
		break;
	case sun_raw_colormap:
		return wuerr(wu_samples_wanted, "RAW colormap encoding");
	default:
		return wuerr(wu_invalid_header, "unknown colormap encoding");
	}

	switch (bitdepth) {
	case 1: case 4:
		img->cs.invert = cm_type == sun_no_colormap;
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
		return wuerr(wu_invalid_header,
			"bitdepth is not 1, 4, 8, 24, or 32");
	}

	img->w = endian32b(header[1]);
	img->h = endian32b(header[2]);
	img->channels = (uint8_t)u32max(bitdepth/8, 1);
	img->bitdepth = (uint8_t)u32min(bitdepth, 8);
	img->align_sh = 1;

	*desc = (struct sun_desc) {
		.ifp = ifp,
		.type = type,
		.colormap_type = cm_type,
	};
	return (cm_type == sun_no_colormap)
		? WU_OK
		: sun_interleave_colormap(desc, img);
}
