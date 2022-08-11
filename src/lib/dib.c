#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#include "raster/bit.h"
#include "raster/endian.h"
#include "raster/file.h"
#include "raster/fmt.h"
#include "raster/mem.h"
#include "raster/unpack.h"
#include "dib.h"

enum dib_rle_marker {
        dib_end_of_scan_line = 0,
        dib_end_of_rle = 1,
        dib_delta = 2,
};

struct ico_buf {
	size_t stride, size;
	unsigned char *buf;
};

static const uint32_t BITFIELD_SHIFT = 16;

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

double dib_cie_to_double(const dib_cie_t f) {
	return (double)f / (1 << 30);
}

double dib_gamma_to_double(const dib_gamma_t f) {
	return (double)f / (1 << 16);
}

static bool load_profile_data(const struct dib_desc *desc, struct wustr *name) {
	fseek(desc->ifp, desc->lcs.profile_off, SEEK_SET);
	if (wustr_malloc(name, file_remaining(desc->ifp))) {
		name->len = fread(name->str, 1, name->len, desc->ifp);
		if (name->len) {
			return true;
		}
		wustr_free(name);
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
		switch (lcs->type) {
		case dib_lcs_calibrated_rgb:
			return color_space_set_primaries_rgb(cs,
				dib_cie_to_double(lcs->r.x),
				dib_cie_to_double(lcs->r.y),
				dib_cie_to_double(lcs->g.x),
				dib_cie_to_double(lcs->g.y),
				dib_cie_to_double(lcs->b.x),
				dib_cie_to_double(lcs->b.y))
			&& color_space_set_gamma_rgb(cs,
				dib_gamma_to_double(lcs->gamma.r),
				dib_gamma_to_double(lcs->gamma.g),
				dib_gamma_to_double(lcs->gamma.b));
		case dib_profile_embedded:
			;struct wustr name;
			if (load_profile_data(desc, &name)) {
				return color_space_set_icc_owned(cs, name.str,
					name.len);
			}
			break;
		case dib_profile_linked:
		case dib_lcs_srgb:
		case dib_lcs_windows_color_space:
			return true;
		}
		return false;
	}
	return true;
}

static size_t rle_loop4(unsigned char *restrict raster, const size_t raster_len,
const unsigned char *restrict rle, const size_t rle_len, const size_t scan_len) {
	size_t i = 0;
	size_t o = 0;
	do {
		const unsigned char repeat = rle[i];
		const unsigned char marker = rle[i+1];
		i += 2;
		if (repeat) {
			if (o + repeat > raster_len) {
				return o;
			}
			const unsigned char val[] = {
				marker >> 4,
				marker & 0x0f,
			};
			memtessel(raster + o, val, sizeof(val), repeat);
			o += repeat;
		} else {
			switch (marker) {
			case dib_end_of_scan_line:
				o += (raster_len - o) % scan_len;
				break;
			case dib_end_of_rle:
				return o;
			case dib_delta:
				if (i >= rle_len) {
					return o;
				}
				const unsigned char x_diff = rle[i];
				const unsigned char y_diff = rle[i+1];
				o += scan_len * y_diff + x_diff;
				i += 2;
				break;
			default:
				;const size_t run_bytes = scanline_length(
					marker, 4, 2);
				if (o + marker > raster_len
				|| i + run_bytes > rle_len) {
					return o;
				}
				unpack_strip(raster + o, rle + i, marker, 4,
					pix_normal, op_unpack);
				o += marker;
				i += run_bytes;
			}
		}
	} while (o < raster_len && i < rle_len);
	return o;
}

static size_t rle_loop(unsigned char *restrict raster,
const size_t raster_len, unsigned char *restrict rle, const size_t rle_len,
const size_t scan_len, const unsigned char pix_size) {
	size_t i = 0;
	size_t o = 0;
	do {
		const unsigned char repeat = rle[i];
		++i;
		if (repeat) {
			const size_t len = repeat * pix_size;
			if (o + len > raster_len) {
				return o;
			}
			memwordset(raster + o, rle + i, pix_size, repeat);
			o += len;
			i += pix_size;
		} else {
			const unsigned char marker = rle[i];
			++i;
			switch (marker) {
			case dib_end_of_scan_line:
				o += (raster_len - o) % scan_len;
				break;
			case dib_end_of_rle:
				return o;
			case dib_delta:
				if (i >= rle_len) {
					return o;
				}
				const unsigned char x_diff = rle[i];
				const unsigned char y_diff = rle[i+1];
				o += scan_len * y_diff + x_diff*pix_size;
				i += 2;
				break;
			default:
				;const size_t run = marker*pix_size;
				const size_t run_bytes = scanline_length(
					run, 8, 2);
				if (o + run > raster_len
				|| i + run_bytes > rle_len) {
					return o;
				}
				memcpy(raster + o, rle + i, run);
				o += run;
				i += run_bytes;
			}
		}
	} while (o < raster_len && i + pix_size < rle_len);
	return o;
}

static bool rle_decode(const struct dib_desc *desc, struct raw_img *img,
unsigned char *restrict rle, const size_t read) {
	const size_t rle_len = read & (~1u);
	if (rle_len && raw_img_alloc_noverify(img)) {
		const size_t row = raw_img_stride(img);
		const size_t dst_len = row * img->h;
		if (desc->compression == dib_4bit_rle) {
			return rle_loop4(img->data, dst_len, rle, rle_len, row);
		}
		return rle_loop(img->data, dst_len, rle, rle_len, row,
			img->channels);
	}
	return false;
}

static uint32_t expand_bits(const uint32_t word, const struct dib_bitfield *p) {
	return (((word >> p->shift) & p->mask) * p->scale) >> BITFIELD_SHIFT;
}

static bool bitfield_decode(const struct dib_desc *desc, struct raw_img *img,
uint8_t *restrict src) {
	if (!raw_img_alloc_noverify(img)) {
		return false;
	}
	const uint8_t bytes = (img->bitdepth > 8) ? 2 : 1;
	const uint8_t ch = (img->channels == 4) ? 4 : 3; // putting both helps perf.

	const size_t stride = scanline_length(img->w, desc->depth, 4);
	for (size_t y = 0; y < img->h; ++y) {
		const uint8_t *s = src + y*stride;
		for (size_t x = 0; x < img->w; ++x) {
			const uint32_t word = (desc->depth == 32)
				? endian32(((uint32_t *)s)[x], little_endian)
				: endian16(((uint16_t *)s)[x], little_endian);

			const size_t o = y * img->w + x;
			for (size_t k = 0; k < ch; ++k) {
				const uint32_t v = expand_bits(word, desc->bf + k);
				if (bytes == 2) {
					((uint16_t *)img->data)[o*ch + k] = (uint16_t)v;
				} else {
					img->data[o*ch + k] = (uint8_t)v;
				}
			}
		}
	}
	return true;
}

bool dib_decode(const struct dib_desc *desc, struct raw_img *img) {
	bool ok = false;
	void *src = malloc(desc->size);
	if (src) {
		const size_t read = fread(src, 1, desc->size, desc->ifp);
		switch ((int)desc->compression) {
		case dib_no_compression:
			img->data = src;
			src = NULL;
			if (desc->depth == 16) {
				endian_loop16(src, little_endian, read/2);
			}
			ok = true;
			break;
		case dib_8bit_rle:
		case dib_4bit_rle:
		case os2_24bit_rle:
			ok = rle_decode(desc, img, src, read);
			break;
		case dib_bitfield:
			ok = bitfield_decode(desc, img, src);
			break;
		}
		free(src);
		if (ok) {
			return dib_get_colorspace(desc, &img->cs);
		}
	}
	return ok;
}

static enum wu_error load_pal(struct dib_desc *desc, struct raw_img *img,
const enum fmt_pal_type type) {
	img->alpha = alpha_ignore;
	struct raster_pal *pal = raw_img_palette_init(img);
	if (pal) {
		return fmt_load_pal(desc->ifp, pal, type, desc->pal_entries);
	}
	return wu_alloc_error;
}

static struct dib_ciexyz load_xyz(uint8_t *buf) {
	return (struct dib_ciexyz) {
		.x = buf_endian32(buf, little_endian),
		.y = buf_endian32(buf+4, little_endian),
		.z = buf_endian32(buf+8, little_endian),
	};
}

static enum wu_error load_colorspace(struct dib_desc *desc, uint8_t *buf) {
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

	uint32_t type;
	memcpy(&type, buf, sizeof(type));
	switch (type) {
	case dib_lcs_calibrated_rgb:
		desc->lcs = (struct dib_lcs) {
			.r = load_xyz(buf + 4),
			.g = load_xyz(buf + 16),
			.b = load_xyz(buf + 28),
			.gamma = {
				.r = buf_endian32(buf + 40, little_endian),
				.g = buf_endian32(buf + 44, little_endian),
				.b = buf_endian32(buf + 48, little_endian),
			},
		};
		break;
	case dib_profile_linked:
	case dib_profile_embedded:
		desc->lcs.profile_off = buf_endian32(buf + 60, little_endian);
		// fallthrough
	case dib_lcs_srgb:
	case dib_lcs_windows_color_space:
		if (desc->type < dib_v5_header) {
			return wu_invalid_header;
		}
		const uint32_t intent = buf_endian32(buf + 52, little_endian);
		switch (intent) {
		case dib_gm_abs_colorimetric:
		case dib_gm_business:
		case dib_gm_graphics:
		case dib_gm_images:
			desc->lcs.intent = intent;
			break;
		default:
			return wu_invalid_header;
		}
		break;
	default:
		return wu_invalid_header;
	}
	desc->lcs.type = type;
	return wu_ok;
}

static enum wu_error load_mask(struct dib_desc *desc, struct raw_img *img,
uint8_t *buf) {
	uint8_t ch = desc->type < dib_v3_info_header ? 3 : 4;
	const uint32_t mask[4] = {
		// Switch mask to BGRA order for consistency
		buf_endian32(buf + 2*4, little_endian),
		buf_endian32(buf + 1*4, little_endian),
		buf_endian32(buf, little_endian),
		buf_endian32(buf + 3*4, little_endian)
	};
	if (!buf[3]) {
		ch = 3;
	}

	struct dib_bitfield *bf = desc->bf;
	uint32_t xor_acc = 0;
	unsigned maxdepth = 0;
	for (uint8_t i = 0; i < ch; ++i) {
		uint32_t rem = mask[i];
		if (!rem) {
			bf[i] = (struct dib_bitfield){0};
			continue;
		}

		unsigned zeroes = 0;
		while (!(rem & 1)) {
			rem >>= 1;
			++zeroes;
		}

		bf[i].shift = zeroes;
		bf[i].mask = rem;

		unsigned ones = 0;
		while ((rem & 1)) {
			rem >>= 1;
			++ones;
		}

		// Mask bits must be continous and non-overlapping
		if (rem || (xor_acc & mask[i])) {
			return wu_invalid_header;
		}
		xor_acc ^= mask[i];

		if (ones > maxdepth) {
			maxdepth = ones;
		}
	}
	if (maxdepth > desc->depth / 2) {
		return wu_invalid_header;
	}

	const bool high_depth = maxdepth > 8;
	const unsigned target = high_depth ? USHRT_MAX : UCHAR_MAX;
	for (int i = 0; i < ch; ++i) {
		if (bf[i].mask) {
			bf[i].scale = (target << BITFIELD_SHIFT) / bf[i].mask + 1;
		}
	}

	img->channels = ch;
	img->bitdepth = high_depth ? 16 : 8;
	img->align_sh = 0;
	return wu_ok;
}

static enum wu_error validate_common(struct dib_desc *desc, struct raw_img *img,
const uint16_t planes, const uint32_t horz_res, const uint32_t vert_res,
const uint32_t colors) {
	if (planes > 1) { // Some files set it to 0
		return wu_invalid_header;
	}
	if (desc->depth <= 8) {
		if (colors > 256) {
			return wu_invalid_header;
		} else if (colors) {
			desc->pal_entries = colors;
		} else {
			desc->pal_entries = 1 << desc->depth;
		}
	}
	raw_img_aspect_ratio(img, (int)vert_res, (int)horz_res);
	return wu_ok;
}

static enum wu_error validate_os2_header(struct dib_desc *desc,
struct raw_img *img, const uint32_t width, const uint32_t height,
const uint16_t depth, const uint32_t compression, const uint32_t size,
const uint16_t storage, const uint32_t color_encoding) {
	if (width < 1 || height < 1) {
		return wu_invalid_header;
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
		return wu_invalid_header;
	}

	switch (compression) {
	case os2_no_compression: break;
	case os2_8bit_rle:
		if (depth != 8 || !size) {
			return wu_invalid_header;
		}
		break;
	case os2_4bit_rle:
		if (depth != 4 || !size) {
			return wu_invalid_header;
		}
		break;
	case os2_1d_huffman:
		return wu_unsupported_feature;
	case os2_24bit_rle:
		if (depth != 24 || !size) {
			return wu_invalid_header;
		}
		break;
	}
	if (storage != 0) {
		(void)storage;
//		return wu_invalid_header;
	}
	if (color_encoding != 0) {
		(void)color_encoding;
//		return wu_invalid_header;
	}
	desc->depth = (unsigned char)depth;
	desc->order = dib_bottom_up;
	desc->compression = (unsigned char)compression;
	desc->size = size;
	return wu_ok;
}

static enum wu_error validate_dib_header(struct dib_desc *desc,
struct raw_img *img, const int32_t width, const int32_t height,
const uint16_t depth, const uint32_t compression, const uint32_t rle_size) {
	if (width < 1) {
		return wu_invalid_header;
	}
	if (!height) {
		return wu_invalid_header;
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
	case 16: case 24: case 32:
		img->channels = (unsigned char)(depth / 8);
		img->bitdepth = 8;
		break;
	default:
		return wu_invalid_header;
	}

	switch (compression) {
	case dib_no_compression:
		if (depth == 16) {
			img->attr = pix_pack_1555;
		}
		break;
	case dib_8bit_rle:
		if (depth != 8 || desc->order == dib_top_down || !rle_size) {
			return wu_invalid_header;
		}
		break;
	case dib_4bit_rle:
		if (depth != 4 || desc->order == dib_top_down || !rle_size) {
			return wu_invalid_header;
		}
		img->bitdepth = 8;
		break;
	case dib_bitfield:
		if (desc->type < dib_info_header || (depth != 16 && depth != 32)) {
			return wu_invalid_header;
		}
		break;
	default:
		return wu_invalid_header;
	}
	desc->depth = (unsigned char)depth;
	desc->compression = (unsigned char)compression;
	desc->size = rle_size;
	return wu_ok;
}

static enum wu_error dib_parse_os2_2x_header(struct dib_desc *desc,
struct raw_img *img) {
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
		return wu_unexpected_eof;
	}

	enum wu_error status = validate_os2_header(desc, img,
		buf_endian32(buf, little_endian),
		buf_endian32(buf + 4, little_endian),
		buf_endian16(buf + 10, little_endian),
		buf_endian32(buf + 12, little_endian),
		buf_endian32(buf + 16, little_endian),
		buf_endian16(buf + 40, little_endian),
		buf_endian32(buf + 52, little_endian));
	if (status != wu_ok) {
		return status;
	}
	status = validate_common(desc, img,
		buf_endian16(buf + 8, little_endian),
		buf_endian32(buf + 20, little_endian),
		buf_endian32(buf + 24, little_endian),
		buf_endian32(buf + 28, little_endian));
	if (status != wu_ok) {
		return status;
	}
	if (desc->depth <= 8) {
		return load_pal(desc, img, fmt_pal_rgbx);
	}
	return wu_ok;
}

static enum wu_error dib_parse_type3_header(struct dib_desc *desc,
struct raw_img *img) {
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
		return wu_unexpected_eof;
	}

	enum wu_error status = validate_dib_header(desc, img,
		(int32_t)buf_endian32(buf, little_endian),
		(int32_t)buf_endian32(buf + 4, little_endian),
		buf_endian16(buf + 10, little_endian),
		buf_endian32(buf + 12, little_endian),
		buf_endian32(buf + 16, little_endian));
	if (status != wu_ok) {
		return status;
	}

	status = validate_common(desc, img,
		buf_endian16(buf + 8, little_endian),
		buf_endian32(buf + 20, little_endian),
		buf_endian32(buf + 24, little_endian),
		buf_endian32(buf + 28, little_endian));
	if (status != wu_ok) {
		return status;
	}

	if (desc->compression == dib_bitfield) {
		if (desc->type == dib_info_header) {
			if (!fread(buf + read, 4*3, 1, desc->ifp)) {
				return wu_unexpected_eof;
			}
		}
		status = load_mask(desc, img, buf + 36);
		if (status != wu_ok) {
			return status;
		}
	}

	if (desc->type >= dib_v4_header) {
		status = load_colorspace(desc, buf + 52);
		if (status != wu_ok) {
			return status;
		}
	}
	if (desc->depth <= 8) {
		return load_pal(desc, img, fmt_pal_rgbx);
	}
	return wu_ok;
}

static enum wu_error dib_parse_core_header(struct dib_desc *desc,
struct raw_img *img) {
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
		return wu_unexpected_eof;
	}

	enum wu_error status = validate_dib_header(desc, img,
		(int16_t)endian16(buf[0], little_endian),
		(int16_t)endian16(buf[1], little_endian),
		endian16(buf[3], little_endian),
		dib_no_compression, 0);
	if (status != wu_ok) {
		return status;
	}
	status = validate_common(desc, img, endian16(buf[2], little_endian),
		0, 0, 0);
	if (status != wu_ok) {
		return status;
	}

	if (desc->depth <= 8) {
		return load_pal(desc, img, fmt_pal_rgb);
	} else if (desc->depth != 24) {
		return wu_invalid_header;
	}
	return wu_ok;
}

static enum wu_error dib_parse_header(struct dib_desc *desc,
struct raw_img *img) {
	/* Common DIB header:
		Offset  Size    Name
		0       u32     Size         // Size of DIB header in bytes
		4
	*/
	uint32_t hsize;
	if (!fread(&hsize, sizeof(hsize), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	hsize = endian32(hsize, little_endian);

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
			return wu_invalid_header;
		}
		desc->is_os2 = trit_true;
		break;
	}

	desc->type = (enum dib_type)hsize;
	raw_img_align(img, 4);
	img->layout = pix_bgra;

	enum wu_error status;
	if (core_header) {
		status = dib_parse_core_header(desc, img);
	} else if (desc->is_os2 == trit_true) {
		status = dib_parse_os2_2x_header(desc, img);
	} else {
		status = dib_parse_type3_header(desc, img);
	}

	if (status == wu_ok) {
		status = raw_img_verify(img);
	}
	if (status != wu_ok) {
		return status;
	}

	const size_t size = scanline_length(img->w, desc->depth, 4) * img->h;
	switch ((int)desc->compression) {
	case 3: // dib_bitfield, os2_1d_huffman
		if (desc->is_os2 == trit_true) {
			break;
		}
		// fallthrough
	case dib_no_compression:
		desc->size = size;
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
	}
	return wu_ok;
}

enum wu_error dib_open_file(struct dib_desc *desc, struct raw_img *img,
FILE *ifp) {
	desc->ifp = ifp;
	return dib_parse_header(desc, img);
}

enum wu_error bmp_parse_header(struct dib_desc *desc, struct raw_img *img) {
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
		return wu_unexpected_eof;
	}

	enum wu_error status = dib_parse_header(desc, img);
	if (status != wu_ok) {
		return status;
	}

	const long bitmap_offset = endian32(buf[2], little_endian);
	fseek(desc->ifp, bitmap_offset, SEEK_SET);
	return status;
}

enum wu_error bmp_open_file(struct dib_desc *desc, FILE *ifp) {
	enum wu_error fail;
	const unsigned char magic[][2] = {
		{'B', 'M'}, // BMP
		{0, 0}, // DDB
	};
	unsigned char sig[2];
	if (fread(sig, sizeof(sig), 1, ifp)) {
		if (!memcmp(magic[0], sig, sizeof(sig))) {
			*desc = (struct dib_desc) {
				.ifp = ifp,
				.is_os2 = trit_what,
			};
			return wu_ok;
		} else if (!memcmp(magic[1], sig, sizeof(sig))) {
			fail = wu_unsupported_feature;
		} else {
			fail = wu_invalid_signature;
		}
	} else {
		fail = wu_unexpected_eof;
	}
	return fail;
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

static void ico_buf_sizes(struct ico_buf *buf, const struct raw_img *img,
const unsigned char depth) {
	buf->stride = scanline_length(img->w, depth, 4);
	buf->size = buf->stride * img->h;
}

static bool ico_buf_load(struct ico_buf *buf, const struct raw_img *img,
const unsigned char depth, FILE *ifp) {
	ico_buf_sizes(buf, img, depth);
	buf->buf = malloc(buf->size);
	if (buf->buf) {
		return fread(buf->buf, 1, buf->size, ifp);
	}
	return false;
}

static void ico_32bit_dec(const struct raw_img *img, struct pix_rgba8 *dst,
const uint8_t *and, const size_t and_stride) {
	for (size_t y = 0; y < img->h; ++y) {
		struct pix_rgba8 *d = dst + img->w * y;
		const uint8_t *a = and + and_stride * y;
		for (size_t x = 0; x < img->w; ++x) {
			if (bit_get(a, x)) {
				d[x].a = 0;
			}
		}
	}
}

static bool ico_word_dec(const struct dib_desc *dib, struct raw_img *img) {
	struct ico_buf dst, and;
	if (!ico_buf_load(&dst, img, dib->depth, dib->ifp)) {
		return false;
	}
	img->data = dst.buf;

	if (!ico_buf_load(&and, img, 1, dib->ifp)) {
		return false;
	}

	if (dib->depth == 32) {
		ico_32bit_dec(img, (struct pix_rgba8 *)dst.buf, and.buf,
			and.stride);
	} else {
		for (size_t y = 0; y < img->h; ++y) {
			uint16_t *d = (uint16_t *)(dst.buf + dst.stride * y);
			const uint8_t *a = and.buf + and.stride * y;
			for (size_t x = 0; x < img->w; ++x) {
				const bool bit = bit_get(a, x);
				int i = (endian16(d[x], little_endian) & 0x7fff)
					| (!bit << 15);
				d[x] = (uint16_t)i;
			}
		}
	}
	free(and.buf);
	return true;
}

static bool ico_truecolor_expands(const struct dib_desc *dib,
struct raw_img *img, struct ico_buf *restrict dst, struct ico_buf *restrict xor,
struct ico_buf *restrict and) {
	ico_buf_sizes(dst, img, 32);
	ico_buf_sizes(xor, img, dib->depth);
	ico_buf_sizes(and, img, 1);

	dst->buf = malloc(dst->size);
	if (!dst->buf) {
		return false;
	}

	xor->buf = malloc(xor->size + and->size);
	if (!xor->buf) {
		free(dst->buf);
		return false;
	}
	and->buf = xor->buf + xor->size;
	return fread(xor->buf, 1, xor->size + and->size, dib->ifp) != 0;
}

static bool ico_24bit_dec(const struct dib_desc *dib, struct raw_img *img) {
	struct ico_buf dst, xor, and;
	if (!ico_truecolor_expands(dib, img, &dst, &xor, &and)) {
		return false;
	}

	for (size_t y = 0; y < img->h; ++y) {
		uint8_t *d = dst.buf + dst.stride * y;
		uint8_t *s = xor.buf + xor.stride * y;
		uint8_t *a = and.buf + and.stride * y;
		for (size_t x = 0; x < img->w; ++x) {
			memcpy(d + x*4, s + x*3, 4);
			d[x*4 + 3] = (bit_get(a, x) ? 0x00 : 0xff);
		}
	}
	free(xor.buf);
	img->data = dst.buf;
	return true;
}

static bool ico_palette_dec(struct dib_desc *dib, struct raw_img *img) {
	struct ico_buf dst, xor, and;
	if (!ico_truecolor_expands(dib, img, &dst, &xor, &and)) {
		return false;
	}

	const size_t instride = scanline_length(img->w, dib->depth, 4);
	const size_t outstride = scanline_length(img->w, 32, 4);
	for (size_t y = 0; y < img->h; ++y) {
		raster_pal_expand(dst.buf + outstride*y, xor.buf + instride*y,
			img->u.palette, img->w, dib->depth);
	}
	ico_32bit_dec(img, (struct pix_rgba8 *)dst.buf, and.buf, and.stride);

	free(xor.buf);
	free(img->u.palette);
	img->u.palette = NULL;
	img->mode = image_mode_raw;
	img->data = dst.buf;
	return true;
}

bool ico_decode(struct ico_desc *desc, struct raw_img *img) {
	struct dib_desc *dib = &desc->dib;
	switch (dib->depth) {
	case 16: case 32:
		return ico_word_dec(dib, img);
	case 24: return ico_24bit_dec(dib, img);
	}
	return ico_palette_dec(dib, img);
}

enum wu_error ico_set_image(struct ico_desc *desc, struct raw_img *img,
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
	struct dib_desc *dib = &desc->dib;
	fseek(dib->ifp, desc->images[i].offset, SEEK_SET);
	dib->is_os2 = trit_false;
	// dib_parse_header() will drop us at the start of the XOR bitmap.
	enum wu_error status = dib_parse_header(dib, img);
	if (status != wu_ok) {
		return status;
	}

	// For bizarre reasons the XOR and AND bitmaps are counted together.
	if (img->h % 2 != 0) {
		return wu_invalid_header;
	}
	img->h /= 2;
	if (dib->depth != 16) {
		img->bitdepth = 8;
		img->channels = 4;
	}

	if (dib->type == dib_info_header && dib->compression == dib_no_compression) {
		return wu_ok;
	}
	return wu_invalid_header;
}

enum wu_error ico_parse_header(struct ico_desc *desc) {
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
		return wu_alloc_error;
	}

	for (uint16_t i = 0; i < desc->count; ++i) {
		/* Each image includes its own DIB header, so we only save the
		 * image location and verify the values here are not outrageous. */
		uint8_t buf[16];
		if (!fread(buf, sizeof(buf), 1, desc->dib.ifp)) {
			return wu_unexpected_eof;
		}

		const uint16_t x = buf_endian16(buf + 4, little_endian);
		const uint16_t y = buf_endian16(buf + 6, little_endian);
		if (desc->type == ico_cursor) {
			desc->images[i].x = x;
			desc->images[i].y = y;
		} else {
			if (x > 1) {
				return wu_invalid_header;
			}
			switch (y) {
			case 0: case 1: case 2: case 4: case 8:
			case 16: case 24: case 32:
				break;
			default:
				return wu_invalid_header;
			}
		}
		desc->images[i].size = buf_endian32(buf + 8, little_endian);
		desc->images[i].offset = buf_endian32(buf + 12, little_endian);
	}
	return wu_ok;
}

enum wu_error ico_open_file(struct ico_desc *desc, FILE *ifp) {
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
		const uint16_t type = endian16(header[1], little_endian);
		const uint16_t count = endian16(header[2], little_endian);
		if (header[0] == 0 && count != 0) {
			switch (type) {
			case ico_icon:
			case ico_cursor:
				desc->dib.ifp = ifp;
				desc->type = type;
				desc->count = count;
				return wu_ok;
			}
		}
		return wu_invalid_header;
	}
	return wu_unexpected_eof;
}
