// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/file.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "raster/unpack.h"
#include "dib.h"

enum dib_rle_marker {
        dib_end_of_scan_line = 0,
        dib_end_of_rle = 1,
        dib_delta = 2,
};

static bool valid_os2_2x(const uint32_t size) {
	if (size >= dib_os2_2x_bitmap_header_min && size <= dib_os2_2x_bitmap_header) {
		return size % 4 == 0 || size == 42 || size == 46;
	}
	return false;
}

const char * dib_compression_str(const enum dib_compression comp) {
	switch (comp) {
	case dib_no_compression: return "None";
	case dib_8bit_rle: return "8bit RLE";
	case dib_4bit_rle: return "4bit RLE";
	case dib_bitfield: return "Bitfield";
	}
	return "???";
}

const char * dib_type_str(const struct dib_desc *desc) {
	switch (desc->type) {
	case dib_core_header: return "BITMAPCOREHEADER";
	case dib_info_header: return "BITMAPINFOHEADER";
	case dib_v2_info_header: return "BITMAPV2INFOHEADER";
	case dib_v3_info_header: return "BITMAPV3INFOHEADER";
	case dib_v4_header: return "BITMAPV4HEADER";
	case dib_v5_header: return "BITMAPV5HEADER";
	case dib_os2_2x_bitmap_header_min: return "OS22XBITMAPHEADER (Minimal)";
	case dib_os2_2x_bitmap_header: return "OS22XBITMAPHEADER";
	}
	if (valid_os2_2x(desc->type)) {
		return "OS22XBITMAPHEADER (Trimmed)";
	}
	return "???";
}

static double dib_cie_to_double(const dib_cie_t f) {
	return (double)f / (1 << 30);
}

static double dib_gamma_to_double(const dib_gamma_t f) {
	return (double)f / (1 << 16);
}

static bool load_profile_data(const struct dib_desc *desc, struct wustr *dst) {
	fseek(desc->ifp, desc->lcs.profile_off, SEEK_SET);
	if (wustr_malloc(dst, file_remaining(desc->ifp))) {
		dst->len = fread(dst->str, 1, dst->len, desc->ifp);
		if (dst->len) {
			return true;
		}
		wustr_free(dst);
	}
	return false;
}

bool dib_get_linked_profile_name(const struct dib_desc *desc,
struct wustr *name) {
	if (desc->type >= dib_v4_header && desc->lcs.type == dib_profile_linked) {
		return load_profile_data(desc, name);
	}
	return false;
}

static bool dib_get_colorspace(const struct dib_desc *desc, struct color_space *cs) {
	if (desc->type >= dib_v4_header) {
		const struct dib_lcs *lcs = &desc->lcs;
		if (lcs->type == dib_profile_embedded) {
			struct wustr icc;
			if (load_profile_data(desc, &icc)) {
				return color_space_set_icc_owned(cs, icc.str,
					icc.len);
			}
			return false;
		}
	}
	return true;
}

static size_t rle_loop4(unsigned char *restrict dst, const size_t dst_len,
const unsigned char *restrict src, const size_t src_len, const size_t scan_len) {
	size_t s = 0;
	size_t d = 0;
	while (d < dst_len && src_len - s >= 2) {
		const unsigned char repeat = src[s];
		const unsigned char marker = src[s+1];
		s += 2;
		if (repeat) {
			if (dst_len - d < repeat) {
				return d;
			}
			const unsigned char val[] = {
				marker >> 4,
				marker & 0x0f,
			};
			memtessel(dst + d, val, sizeof(val), repeat);
			d += repeat;
		} else {
			switch (marker) {
			case dib_end_of_scan_line:
				d += (dst_len - d) % scan_len;
				break;
			case dib_end_of_rle:
				return dst_len;
			case dib_delta:
				if (src_len - s < 2) {
					return d;
				}
				const unsigned char x_diff = src[s];
				const unsigned char y_diff = src[s+1];
				d += scan_len * y_diff + x_diff;
				s += 2;
				break;
			default:
				;const size_t run_bytes = strip_length(
					marker, 4, 1);
				if (dst_len - d < marker
				|| src_len - s < run_bytes) {
					return d;
				}
				unpack_strip(dst + d, src + s, marker, 4,
					pix_normal, big_endian, op_repack, NULL);
				d += marker;
				s += run_bytes;
			}
		}
	}
	return d;
}

static size_t rle_loop(unsigned char *restrict dst,
const size_t dst_len, unsigned char *restrict src, const size_t src_len,
const size_t scan_len, const unsigned char pix_size) {
	size_t s = 0;
	size_t d = 0;
	while (d < dst_len && src_len - s >= pix_size + 1u) {
		const unsigned char repeat = src[s];
		++s;
		if (repeat) {
			const size_t len = repeat * pix_size;
			if (dst_len - d < len) {
				return d;
			}
			memwordset(dst + d, src + s, pix_size, repeat);
			d += len;
			s += pix_size;
		} else {
			const unsigned char marker = src[s];
			++s;
			switch (marker) {
			case dib_end_of_scan_line:
				d += (dst_len - d) % scan_len;
				break;
			case dib_end_of_rle:
				return dst_len;
			case dib_delta:
				if (src_len - s < 2) {
					return d;
				}
				const unsigned char x_diff = src[s];
				const unsigned char y_diff = src[s+1];
				d += scan_len * y_diff + x_diff*pix_size;
				s += 2;
				break;
			default:
				;const size_t run = marker*pix_size;
				const size_t run_bytes = strip_length(run, 8, 1);
				if (dst_len - d < run
				|| src_len - s < run_bytes) {
					return d;
				}
				memcpy(dst + d, src + s, run);
				d += run;
				s += run_bytes;
			}
		}
	}
	return d;
}

static size_t rle_decode(const struct dib_desc *desc, struct wuimg *img) {
	uint8_t *rle = malloc(desc->size);
	size_t w = 0;
	if (rle) {
		const size_t read = fread(rle, 1, desc->size, desc->ifp);
		const size_t rle_len = read & (~1u); // len should be even
		const size_t row = wuimg_stride(img);
		const size_t dst_len = row * img->h;
		if (desc->compression == dib_4bit_rle) {
			w = rle_loop4(img->data, dst_len, rle, rle_len, row);
		} else {
			w = rle_loop(img->data, dst_len, rle, rle_len, row,
				img->channels);
		}
		free(rle);
	}
	return w;
}

struct wu_st dib_decode(const struct dib_desc *desc, struct wuimg *img) {
	size_t w = 0;
	switch ((int)desc->compression) {
	case dib_no_compression:
	case dib_bitfield:
		w = fmt_load_raster_swap(img, desc->ifp, little_endian);
		break;
	case dib_8bit_rle:
	case dib_4bit_rle:
	case os2_24bit_rle:
		w = rle_decode(desc, img);
		break;
	}
	struct wu_st st = wuerr_partial(w, wuimg_size(img));
	if (!dib_get_colorspace(desc, &img->cs)) {
		st.msg = "failed to load icc data";
	}
	return st;
}

static struct dib_ciexyz load_xyz(uint8_t *buf) {
	return (struct dib_ciexyz) {
		.x = buf_endian32l(buf),
		.y = buf_endian32l(buf+4),
		.z = buf_endian32l(buf+8),
	};
}

static struct wu_st load_colorspace(struct dib_desc *desc, struct wuimg *img,
uint8_t *buf) {
	/* BITMAPV4HEADER (after previous fields):
		Offset  Type    Name
		0       u32     ColorSpaceType
		4       CIEXYZ  RedCoords
		16      CIEXYZ  GreenCoords
		28      CIEXYZ  BlueCoords
		40      u32     GammaRed
		44      u32     GammaGreen
		48      u32     GammaBlue
		52

	 * BITMAPV5HEADER additional fields:
		Offset  Type    Name
		52      u32     RenderingIntent
		56      u32     ProfileData
		60      u32     ProfileSize
		64      u32     Reserved
		68

	 * CIEXYZ struct:
		Offset  Type    Name
		0       u32     XCoord
		4       u32     YCoord
		8       u32     ZCoord
	*/

	struct dib_lcs *lcs = &desc->lcs;
	lcs->type = buf_endian32l(buf);
	switch (lcs->type) {
	case dib_lcs_calibrated_rgb:
		lcs->r = load_xyz(buf + 4),
		lcs->g = load_xyz(buf + 16),
		lcs->b = load_xyz(buf + 28),
		lcs->gamma = (struct dib_gamma) {
			.r = buf_endian32l(buf + 40),
			.g = buf_endian32l(buf + 44),
			.b = buf_endian32l(buf + 48),
		};
		if (!memchk(buf + 4, 0, 12*3)
		&& lcs->gamma.r && lcs->gamma.g && lcs->gamma.b) {
			const bool ok = color_space_set_primaries_rgb(&img->cs,
				dib_cie_to_double(desc->lcs.r.x),
				dib_cie_to_double(desc->lcs.r.y),
				dib_cie_to_double(desc->lcs.g.x),
				dib_cie_to_double(desc->lcs.g.y),
				dib_cie_to_double(desc->lcs.b.x),
				dib_cie_to_double(desc->lcs.b.y))
			&& color_space_set_gamma_rgb(&img->cs,
				dib_gamma_to_double(desc->lcs.gamma.r),
				dib_gamma_to_double(desc->lcs.gamma.g),
				dib_gamma_to_double(desc->lcs.gamma.b));
			if (!ok) {
				return WUERR_HERE(wu_alloc_error);
			}
		}
		return WU_OK;
	case dib_profile_linked:
	case dib_profile_embedded:
		if (desc->type < dib_v5_header) {
			return wuerr(wu_invalid_header,
				"Color profiles used with DIB type < 5");
		}
		desc->lcs.profile_off = buf_endian32l(buf + 60);
		break;
	case dib_lcs_srgb:
	case dib_lcs_windows_color_space:
		break;
	}
	if (desc->type >= dib_v5_header) {
		const uint32_t intent = buf_endian32l(buf + 52);
		switch (intent) {
		case dib_gm_abs_colorimetric:
		case dib_gm_business:
		case dib_gm_graphics:
		case dib_gm_images:
			desc->lcs.intent = intent;
		}
	}
	return WU_OK;
}

static enum wu_error load_mask(struct dib_desc *desc, struct wuimg *img,
uint8_t *buf) {
	uint8_t ch = desc->type < dib_v3_info_header ? 3 : 4;
	const uint32_t mask[4] = {
		buf_endian32l(buf),
		buf_endian32l(buf + 1*4),
		buf_endian32l(buf + 2*4),
		buf_endian32l(buf + 3*4)
	};
	return wuimg_bitfield_from_mask(img, mask, ch, desc->depth);
}

static struct wu_st validate_common(struct dib_desc *desc, struct wuimg *img,
const uint16_t planes, const uint32_t horz_res, const uint32_t vert_res,
const uint32_t colors) {
	if (planes > 1) { // Some files set it to 0
		return wuerr(wu_invalid_header, "planes > 1");
	}
	if (desc->depth <= 8) {
		if (colors > 256) {
			return wuerr(wu_invalid_header, "Excessive pal entries");
		} else if (colors) {
			desc->pal_entries = colors;
		} else {
			desc->pal_entries = 1 << desc->depth;
		}
	}
	wuimg_aspect_ratio(img, vert_res, horz_res);
	return WU_OK;
}

static struct wu_st validate_os2_header(struct dib_desc *desc,
struct wuimg *img, const uint32_t width, const uint32_t height,
const uint16_t depth, const uint32_t compression, const uint32_t size,
const uint16_t storage, const uint32_t color_encoding) {
	if (width < 1 || height < 1) {
		return wuerr(wu_invalid_header, "width or height < 1");
	}
	img->w = width;
	img->h = height;
	img->mirror = true;

	switch (depth) {
	case 1: case 4: case 8:
		img->channels = 1;
		img->bitdepth = (unsigned char)depth;
		break;
	case 24:
		img->channels = 3;
		img->bitdepth = 8;
		break;
	default:
		return wuerr(wu_invalid_header, "Invalid depth for OS/2");
	}

	switch (compression) {
	case os2_no_compression: break;
	case os2_8bit_rle:
		if (depth != 8 || !size) {
			return wuerr(wu_invalid_header, "Bad 8bit RLE params");
		}
		break;
	case os2_4bit_rle:
		if (depth != 4 || !size) {
			return wuerr(wu_invalid_header, "Bad 4bit RLE params");
		}
		break;
	case os2_1d_huffman:
		return wuerr(wu_unsupported_feature,
			"Huffman compression not supported");
	case os2_24bit_rle:
		if (depth != 24 || !size) {
			return wuerr(wu_invalid_header, "Bad 24bit RLE params");
		}
		break;
	default:
		return wuerr(wu_invalid_header, "Invalid OS/2 compression");
	}
	desc->depth = (unsigned char)depth;
	desc->order = dib_bottom_up;
	desc->compression = (unsigned char)compression;
	desc->size = size;
	return wuerr(wu_ok, (storage != 0 || color_encoding != 0)
		? "Ignoring non-zero `storage` and `color_encoding`" : NULL);
}

static struct wu_st validate_dib_header(struct dib_desc *desc,
struct wuimg *img, const int32_t width, const int32_t height,
const uint16_t depth, const uint32_t compression, const uint32_t rle_size) {
	if (width < 1) {
		return wuerr(wu_invalid_header, "width < 1");
	}
	if (!height) {
		return wuerr(wu_invalid_header, "height == 0");
	}
	desc->order = (height > 0) ? dib_bottom_up : dib_top_down;
	img->w = (unsigned)width;
	img->h = (unsigned)(height > 0 ? height : -height);
	img->mirror = desc->order == dib_bottom_up;

	switch (depth) {
	case 2: /* Windows CE */
	case 1: case 4: case 8:
		img->channels = 1;
		img->bitdepth = (unsigned char)depth;
		break;
	case 16: case 32:
		if (desc->type < dib_info_header) {
			return wuerr(wu_invalid_header,
				"Invalid depth for type 2 DIB");
		}
		// fallthrough
	case 24:
		img->channels = (unsigned char)(depth / 8);
		img->bitdepth = 8;
		break;
	default:
		return wuerr(wu_invalid_header, "Invalid DIB depth");
	}

	switch (compression) {
	case dib_no_compression:
		if (depth == 16) {
			img->channels = 1;
			img->bitdepth = 16;
			img->layout = pix_bgra;
			if (!wuimg_bitfield_from_id(img, 0x1555)) {
				return WUERR_HERE(wu_alloc_error);
			}
		}
		break;
	case dib_8bit_rle:
		if (depth != 8 || desc->order == dib_top_down || !rle_size) {
			return wuerr(wu_invalid_header, "Bad 8-bit RLE params");
		}
		break;
	case dib_4bit_rle:
		if (depth != 4 || desc->order == dib_top_down || !rle_size) {
			return wuerr(wu_invalid_header, "Bad 4-bit RLE params");
		}
		img->bitdepth = 8;
		break;
	case dib_bitfield:
		if (desc->type < dib_info_header || (depth != 16 && depth != 32)) {
			return wuerr(wu_invalid_header, "Bad bitfield params");
		}
		img->channels = 1;
		img->bitdepth = (unsigned char)depth;
		break;
	default:
		return wuerr(wu_unsupported_feature, "Unsupported DIB compression");
	}
	desc->depth = (unsigned char)depth;
	desc->compression = (unsigned char)compression;
	desc->size = rle_size;
	return WU_OK;
}

static struct wu_st dib_parse_os2_2x_header(struct dib_desc *desc,
struct wuimg *img) {
	/* OS/2 v2 header (after header size field)
		Offset  Size    Name
		0       u32     Width           // Width in pixels
		4       u32     Height          // Height in pixels
		8       i16     Planes          // Nr of color planes (always 1)
		10      i16     BitsPerPixel
		12      u32     Compression     // Compression method
		16      u32     RLEBitmapSize
		20      u32     HorzResolution  // In 'Units'
		24      u32     VertResolution  // In 'Units'
		28      u32     ColorsUsed      // Nr of palette colors, or 0
		32      u32     ColorsImportant // Nr of important colors
		36      i16     Units           // Always 0 (pixels per meter)
		38      i16     Padding
		40      i16     ScanlineStorage // Always 0 (left-to-right, bottom-up)
		42      i16     HalftoneAlgorithm
		44      u32     HalftoneVar1
		48      u32     HalftoneVar2
		52      u32     ColorEncoding   // Always 0 (RGB)
		56      u32     Identifier      // Reserved for application use
		60

	 * If the header size value is less than 64, the missing values are
	 * assumed to be 0.
	*/

	uint8_t buf[60] = {0};
	if (!fread(buf, desc->type - 4, 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct wu_st status = validate_os2_header(desc, img,
		buf_endian32l(buf),
		buf_endian32l(buf + 4),
		buf_endian16l(buf + 10),
		buf_endian32l(buf + 12),
		buf_endian32l(buf + 16),
		buf_endian16l(buf + 40),
		buf_endian32l(buf + 52));
	if (wu_isok(status)) {
		status = validate_common(desc, img,
			buf_endian16l(buf + 8),
			buf_endian32l(buf + 20),
			buf_endian32l(buf + 24),
			buf_endian32l(buf + 28));
	}
	return status;
}

static struct wu_st dib_parse_type3_header(struct dib_desc *desc,
struct wuimg *img) {
	/* Type 3 and up DIB header (after header size field)

	 * BITMAPINFOHEADER:
		Offset  Size    Name
		0       i32     Width           // Width in pixels
		4       i32     Height          // Height in pixels
		8       i16     Planes          // Nr of color planes (always 1)
		10      i16     BitsPerPixel
		12      u32     Compression     // Compression method
		16      u32     RLEBitmapSize
		20      i32     HorzResolution  // In pixels per meter
		24      i32     VertResolution  // In pixels per meter
		28      u32     ColorsUsed      // Nr of palette colors, or 0
		32      u32     ColorsImportant // Nr of important colors
		36

	 * Additional fields when Compression == 3 or when BITMAPV2INFOHEADER
	 * is used:
		Offset  Size    Name
		36      u32     RedMask
		40      u32     GreenMask
		44      u32     BlueMask
		48

	 * BITMAPV3INFOHEADER additional field:
		Offset  Size    Name
		48      u32     AlphaMask
		52

	 * For BITMAPV4HEADER and BITMAPV5HEADER, see load_colorspace().
	 * Afterwards comes the palette if Depth <= 8.
	*/

	uint8_t buf[dib_v5_header - 4];
	const size_t read = desc->type - 4;
	if (!fread(buf, read, 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct wu_st status = validate_dib_header(desc, img,
		(int32_t)buf_endian32l(buf),
		(int32_t)buf_endian32l(buf + 4),
		buf_endian16l(buf + 10),
		buf_endian32l(buf + 12),
		buf_endian32l(buf + 16));
	if (!wu_isok(status)) {
		return status;
	}

	status = validate_common(desc, img,
		buf_endian16l(buf + 8),
		buf_endian32l(buf + 20),
		buf_endian32l(buf + 24),
		buf_endian32l(buf + 28));
	if (!wu_isok(status)) {
		return status;
	}

	if (desc->compression == dib_bitfield) {
		if (desc->type == dib_info_header) {
			if (!fread(buf + read, 4*3, 1, desc->ifp)) {
				return WUERR_HERE(wu_unexpected_eof);
			}
		}
		img->layout = pix_rgba;
		enum wu_error err = load_mask(desc, img, buf + 36);
		if (err != wu_ok) {
			return wuerr(err, "Couldn't load mask");
		}
	}

	if (desc->type >= dib_v4_header) {
		status = load_colorspace(desc, img, buf + 52);
	}
	return status;
}

static struct wu_st dib_parse_core_header(struct dib_desc *desc,
struct wuimg *img) {
	/* Type 2 DIB header (after header size)
		Offset  Size    Name
		0       u16     Width           // Image width in pixels
		2       u16     Height          // Image height in pixels
		4       i16     Planes          // Nr of color planes. Always 1
		6       i16     BitsPerPixel    // Nr of bits per pixel
		8

	 * For OS/2, width and height are unsigned. There's no reliable way of
	 * telling them apart.
	*/

	uint16_t buf[4];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct wu_st status = validate_dib_header(desc, img,
		(int16_t)endian16l(buf[0]),
		(int16_t)endian16l(buf[1]),
		endian16l(buf[3]),
		dib_no_compression, 0);
	if (!wu_isok(status)) {
		return status;
	}
	return validate_common(desc, img, endian16l(buf[2]), 0, 0, 0);
}

static struct wu_st parse_header(struct dib_desc *desc,
struct wuimg *img) {
	/* Common DIB header:
		Offset  Size    Name
		0       u32     Size         // Size of DIB header in bytes
		4
	*/
	uint32_t hsize;
	if (!fread(&hsize, sizeof(hsize), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	hsize = endian32l(hsize);

	bool core_header = false;
	switch (hsize) {
	case dib_core_header:
		core_header = true;
		break;
	case dib_info_header:
	case dib_v2_info_header:
	case dib_v3_info_header:
	case dib_v4_header:
	case dib_v5_header:
		break;
	case dib_os2_2x_bitmap_header:
	case dib_os2_2x_bitmap_header_min:
		desc->is_os2 = trit_true;
		break;
	default:
		if (desc->is_os2 == trit_false || !valid_os2_2x(hsize)) {
			return wuerr(wu_invalid_header, "Bad DIB header size");
		}
		desc->is_os2 = trit_true;
		break;
	}

	desc->type = (enum dib_type)hsize;
	img->align_sh = 2;
	img->layout = pix_bgra;

	struct wu_st status;
	if (core_header) {
		status = dib_parse_core_header(desc, img);
	} else if (desc->is_os2 == trit_true) {
		status = dib_parse_os2_2x_header(desc, img);
	} else {
		status = dib_parse_type3_header(desc, img);
	}
	if (!wu_isok(status)) {
		return status;
	}

	if (desc->depth <= 8) {
		img->alpha = alpha_ignore;
		status = wuimg_palette_from_file(img, (core_header ? 3 : 4),
			desc->pal_entries, desc->ifp);
		if (!wu_isok(status)) {
			return status;
		}
	}

	enum wu_error err = wuimg_verify(img);
	if (err != wu_ok) {
		return WUERR_HERE(err);
	}

	const size_t size = strip_length(img->w, desc->depth, 2) * img->h;
	switch ((int)desc->compression) {
	case 3: // dib_bitfield, os2_1d_huffman
		if (desc->is_os2 == trit_true) {
			break;
		}
		desc->size = size;
		break;
	case dib_no_compression:
		desc->size = size;
		/* Depths 16 and 32 have padding bits that some encoders use as
		 * alpha. However, encoders that actually follow the spec will
		 * have left them unset, which would then display as a empty
		 * image. Hence, this. */
		img->alpha = alpha_ignore;
		break;
	case dib_8bit_rle:
	case dib_4bit_rle:
	case os2_24bit_rle:
		/* E.g. 0x00 0x03 0xff 0xff 0xff... -> 0xff 0xff 0xff...
		 * This is ignoring the obvious infinite 0x00 0x02 0x00 0x00 */
		;const size_t pathological_rle = size * 5 / 3;
		if (pathological_rle < desc->size) {
			desc->size = pathological_rle;
		}
		break;
	default:
		return wuerr(wu_unsupported_feature,
			"Unsupported compression type");
	}
	return WU_OK;
}

struct wu_st dib_parse_header(struct dib_desc *desc, struct wuimg *img) {
	if (!desc->bmp_header) {
		return parse_header(desc, img);
	}

	/* Minimum non-type-1 BMP header (after magic bytes)

		Offset	Size    Name
		0       u32     FileSize     // In bytes. Usually 0
		4       i16     XHotSpot     // Valid only for OS/2 icons and
		6       i16     YHotSpot     //   pointers. Reserved in Windows
		8       u32     BitmapOffset // Start offset of bitmap in bytes
		12

	 * DIB header follows afterwards.
	*/

	uint32_t buf[3];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const struct wu_st status = parse_header(desc, img);
	if (wu_isok(status)) {
		const long bitmap_offset = endian32l(buf[2]);
		fseek(desc->ifp, bitmap_offset, SEEK_SET);
	}
	return status;
}

struct wu_st dib_open_file(struct dib_desc *desc, FILE *ifp, const bool is_bmp,
const enum trit is_os2) {
	*desc = (struct dib_desc) {
		.ifp = ifp,
		.bmp_header = is_bmp,
		.is_os2 = is_os2,
	};
	if (is_bmp) {
		const unsigned char bmp[] = {'B', 'M'};
		const unsigned char jigsaw[] = {'J', 'G'};
		const unsigned char ddb[] = {0, 0};
		unsigned char sig[2];
		if (fread(sig, sizeof(sig), 1, ifp)) {
			if (!memcmp(bmp, sig, sizeof(sig))
			|| !memcmp(jigsaw, sig, sizeof(sig))) {
				return WU_OK;
			} else if (!memcmp(ddb, sig, sizeof(sig))) {
				return wuerr(wu_unsupported_feature,
					"DDB files not supported");
			}
			return WUERR_HERE(wu_invalid_signature);
		}
		return WUERR_HERE(wu_unexpected_eof);
	}
	return WU_OK;
}

/* ICO functions */
const char * ico_type_str(enum ico_type type) {
	switch (type) {
	case ico_icon: return "Icon";
	case ico_cursor: return "Cursor";
	}
	return "???";
}

void ico_cleanup(struct ico_desc *desc) {
	free(desc->images);
}

struct ico_buf {
	size_t stride, read, lines;
	unsigned char *buf;
};

static size_t ico_buf_sizes(struct ico_buf *buf, const struct wuimg *img,
const unsigned char depth) {
	buf->stride = strip_length(img->w, depth, 2);
	buf->lines = 0;
	return buf->stride * img->h;
}

static void ico_buf_load(struct ico_buf *buf, const struct wuimg *img,
const unsigned char depth, FILE *ifp) {
	const size_t size = ico_buf_sizes(buf, img, depth);
	buf->buf = calloc(size, 1);
	if (buf->buf) {
		buf->read = fread(buf->buf, 1, size, ifp);
		buf->lines = zuceildiv(buf->read, buf->stride);
	}
}

static void ico_32bit_join_line(struct pix_rgba8 *dst,
const uint8_t *restrict and, const size_t w) {
	for (size_t x = 0; x < w; ++x) {
		if (bit_get(and, x)) {
			dst[x].a = 0;
		}
	}
}

static struct wu_st ico_word_dec(const struct dib_desc *dib, struct wuimg *img) {
	const size_t read = fmt_load_raster(img, dib->ifp);
	struct ico_buf and = {0};
	ico_buf_load(&and, img, 1, dib->ifp);

	const size_t dst_stride = wuimg_stride(img);
	if (dib->depth == 32) {
		for (size_t y = 0; y < and.lines; ++y) {
			ico_32bit_join_line(
				(struct pix_rgba8 *)(img->data + dst_stride*y),
				and.buf + and.stride*y, img->w);
		}
	} else {
		for (size_t y = 0; y < and.lines; ++y) {
			uint16_t *d = (uint16_t *)(img->data + dst_stride*y);
			const uint8_t *a = and.buf + and.stride * y;
			for (size_t x = 0; x < img->w; ++x) {
				const bool bit = bit_get(a, x);
				int i = (endian16l(d[x]) & 0x7fff)
					| (!bit << 15);
				d[x] = (uint16_t)i;
			}
		}
	}
	free(and.buf);
	return wuerr_partial(read + and.read, (dst_stride + and.stride)*img->h);
}

static bool ico_truecolor_expands(const struct dib_desc *dib,
struct wuimg *img, struct ico_buf *restrict xor, struct ico_buf *restrict and) {
	const size_t x_size = ico_buf_sizes(xor, img, dib->depth);
	const size_t a_size = ico_buf_sizes(and, img, 1);

	xor->buf = calloc(x_size + a_size, 1);
	if (!xor->buf) {
		return false;
	}
	and->buf = xor->buf + x_size;
	const size_t total = fread(xor->buf, 1, x_size + a_size, dib->ifp);
	xor->read = zumin(total, x_size);
	xor->lines = zuceildiv(xor->read, xor->stride);
	and->read = zumax(total, x_size) - x_size;
	and->lines = zuceildiv(and->read, and->stride);
	return true;
}

static struct wu_st ico_24bit_dec(const struct dib_desc *dib,
struct wuimg *img) {
	struct ico_buf xor, and;
	if (!ico_truecolor_expands(dib, img, &xor, &and)) {
		return WUERR_HERE(wu_alloc_error);
	}

	struct pix_rgba8 *dst = (struct pix_rgba8 *)img->data;
	size_t y = 0;
	while (y < and.lines) {
		struct pix_rgba8 *d = dst + img->w * y;
		uint8_t *s = xor.buf + xor.stride * y;
		uint8_t *a = and.buf + and.stride * y;
		for (size_t x = 0; x < img->w; ++x) {
			memcpy(d + x, s + x*3, 4);
			d[x].a = (bit_get(a, x) ? 0x00 : 0xff);
		}
		++y;
	}
	while (y < xor.lines) {
		struct pix_rgba8 *d = dst + img->w * y;
		uint8_t *s = xor.buf + xor.stride * y;
		for (size_t x = 0; x < img->w; ++x) {
			memcpy(d + x, s + x*3, 4);
		}
	}
	free(xor.buf);
	return wuerr_partial(xor.read + and.read,
		(xor.stride + and.stride)*img->h);
}

static struct wu_st ico_palette_dec(struct dib_desc *dib, struct wuimg *img,
const struct palette *pal) {
	struct ico_buf xor, and;
	if (!ico_truecolor_expands(dib, img, &xor, &and)) {
		return WUERR_HERE(wu_alloc_error);
	}

	for (size_t y = 0; y < xor.lines; ++y) {
		struct pix_rgba8 *d = (struct pix_rgba8 *)img->data + img->w*y;
		palette_expand(d, xor.buf + xor.stride*y, pal, img->w,
			dib->depth);
		if (y < and.lines) {
			ico_32bit_join_line(d, and.buf + and.stride*y, img->w);
		}
	}
	free(xor.buf);
	return wuerr_partial(xor.read + and.read,
		(xor.stride + and.stride)*img->h);
}

struct wu_st ico_decode(struct ico_desc *desc, struct wuimg *img) {
	struct dib_desc *dib = &desc->dib;
	switch (dib->depth) {
	case 16: case 32:
		return ico_word_dec(dib, img);
	case 24: return ico_24bit_dec(dib, img);
	}
	return ico_palette_dec(dib, img, desc->pal);
}

struct wu_st ico_set_image(struct ico_desc *desc, struct wuimg *img,
const uint16_t i) {
	/* ICO image components:
		BITMAPINFOHEADER
		Palette
		XORMask
		ANDMask
	 * The image data is meant to be composited over a background, hence
	 * the Mask names. The AND mask sets whether the background is cleared
	 * first as in a AND operation (so it is the opposite of Alpha)
	 * while the XOR mask contains the normal image data. */
	palette_unref(desc->pal);
	desc->pal = NULL;
	struct dib_desc *dib = &desc->dib;
	fseek(dib->ifp, desc->images[i].offset, SEEK_SET);
	dib->is_os2 = trit_false;
	// dib_parse_header() will drop us at the start of the XOR bitmap.
	struct wu_st status = dib_parse_header(dib, img);
	if (!wu_isok(status)) {
		return status;
	}

	// For bizarre reasons the XOR and AND bitmaps are counted together.
	if (img->h % 2 != 0) {
		return wuerr(wu_invalid_header, "Odd height");
	}
	img->h /= 2;
	if (dib->depth != 16) {
		img->bitdepth = 8;
		img->channels = 4;
		if (img->mode == image_mode_palette) {
			desc->pal = img->u.palette;
			img->u.palette = NULL;
			img->mode = image_mode_raw;
		}
	}

	if (dib->type == dib_info_header && dib->compression == dib_no_compression) {
		return WU_OK;
	}
	return wuerr(wu_invalid_header, "Bad DIB type in ICO file");
}

static struct wu_st ico_read_entries(struct ico_desc *desc) {
	/* ICO dir entry (one for each image, stored continuously):
		Offset  Size    Name
		0       BYTE    Width
		1       BYTE    Height
		2       BYTE    ColorCount
		3       BYTE    Reserved     // Should be 0, but Windows ignores it
		4       i16     Planes       // XHotspot for cursors
		6       i16     BitsPerPixel // YHotspot for cursors
		8       u32     ImageSize
		12      u32     ImageOffset
		16
	*/

	desc->images = malloc(sizeof(*desc->images) * desc->count);
	if (!desc->images) {
		return WUERR_HERE(wu_alloc_error);
	}

	for (uint16_t i = 0; i < desc->count; ++i) {
		/* Each image includes its own DIB header, so we only save the
		 * image location and verify the values here are not outrageous. */
		uint8_t buf[16];
		if (!fread(buf, sizeof(buf), 1, desc->dib.ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		}

		const uint16_t x = buf_endian16l(buf + 4);
		const uint16_t y = buf_endian16l(buf + 6);
		if (desc->type == ico_cursor) {
			desc->images[i].x = x;
			desc->images[i].y = y;
		} else {
			if (x > 1) {
				return wuerr(wu_invalid_header, "planes > 1");
			}
			switch (y) {
			case 0: case 1: case 2: case 4: case 8:
			case 16: case 24: case 32:
				break;
			default:
				return wuerr(wu_invalid_header, "bad ico depth");
			}
		}
		desc->images[i].size = buf_endian32l(buf + 8);
		desc->images[i].offset = buf_endian32l(buf + 12);
	}
	return WU_OK;
}

struct wu_st ico_parse(struct ico_desc *desc, FILE *ifp) {
	/* ICO header:
		Offset  Size    Name
		0       i16     Reserved   // 0
		2       i16     Type       // 1 for icons, 2 for cursors
		4       i16     ImageCount
		6
	*/

	*desc = (struct ico_desc){0};

	uint16_t header[3];
	if (fread(header, sizeof(header), 1, ifp)) {
		const uint16_t type = endian16l(header[1]);
		const uint16_t count = endian16l(header[2]);
		if (header[0] == 0 && count != 0) {
			switch (type) {
			case ico_icon:
			case ico_cursor:
				desc->dib.ifp = ifp;
				desc->type = type;
				desc->count = count;
				return ico_read_entries(desc);
			}
		}
		return WUERR_HERE(wu_invalid_header);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

#ifdef WU_ENABLE_BMZ
#include <zlib.h>

void bmz_cleanup(struct bmz_desc *desc) {
	fclose(desc->bmp.ifp);
	free(desc->buf);
}

struct wu_st bmz_open(struct bmz_desc *desc, struct mparser mp) {
	const uint8_t magic[4] = {'Z', 'L', 'C', '3'};
	struct wu_st st = (struct wu_st) {
		.st = fmt_sigcmp_mem(magic, sizeof(magic), &mp),
	};
	if (wu_isok(st)) {
		const struct wuptr z = mp_remaining(&mp);
		if (z.len > 4) {
			uLong orig = buf_endian32l(z.ptr);
			uint8_t *buf = malloc(orig);
			if (buf) {
				uncompress(buf, &orig, z.ptr + 4, z.len - 4);
				if (orig) {
					FILE *ifp = fmemopen(buf, orig, "r");
					if (ifp) {
						st = dib_open_file(&desc->bmp,
							ifp, true, trit_false);
						if (wu_isok(st)) {
							desc->buf = buf;
							return st;
						}
						fclose(ifp);
					} else {
						st = WUERR_HERE(wu_alloc_error);
					}
				} else {
					st = WUERR_HERE(wu_unexpected_eof);
				}
				free(buf);
			} else {
				st = WUERR_HERE(wu_alloc_error);
			}
		} else {
			st = WUERR_HERE(wu_unexpected_eof);
		}
	} else {
		st = WUERR_HERE(st.st);
	}
	return st;
}

#endif /* WU_ENABLE_BMZ */
