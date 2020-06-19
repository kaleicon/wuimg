#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "common.h"
#include "common_unpack.h"
#include "common_composite.h"
#include "lib_bmp.h"

void bmp_cleanup(const struct bmp_desc *desc) {
	free(desc->pal);
}

unsigned int bmp_get_row_alignment(const struct bmp_desc *desc) {
	switch (desc->bitdepth) {
	case 1:
		return 1;
	case 4:
		if (desc->compression != bmp_4bit_rle) {
			return 1;
		}
		// Fallthrough
	case 2:
	case 8:
		if (desc->pal) {
			return 1;
		}
	}
	return 4;
}

static void check_decode(const unsigned char *end,
const unsigned char *expected_end) {
	if (end < expected_end - 3) { // -3 for alignment.
		puts("BMP warning: Run-length decoding didn't fill the whole "
			"buffer. Output may contain garbage.");
	}
}

static unsigned char * uncompressed_expand(const struct bmp_desc *desc,
unsigned char *raster) {
	const unsigned char ch = 3;
	size_t dims = desc->w * desc->h;
	if (desc->pal) {
		dims *= ch;
	}

	unsigned char *output = malloc(dims);
	if (!output) {
		free(raster);
		return NULL;
	}

	if (desc->pal) {
		strip_colormap(output, raster, desc->pal, desc->w, desc->h, 4,
			desc->bitdepth, ch);
	} else {
		strip_unpack(output, raster, desc->w, desc->h, 4,
			desc->bitdepth, unpack);
	}

	free(raster);
	return output;
}

static unsigned char * uncompressed_decode(const struct bmp_desc *desc,
unsigned char *raster) {
	switch (desc->bitdepth) {
	case 8:
		if (!desc->pal) {
			return raster;
		}
		// Fallthrough
	case 4:
	case 2:
	case 1:
		return uncompressed_expand(desc, raster);
	case 24:
		return raster;
	}
	free(raster);
	return NULL;
}

static unsigned char * rle_loop4(unsigned char *restrict raster,
const unsigned char *restrict raster_limit, unsigned char *restrict rle,
const unsigned char *restrict rle_limit, const size_t scan_len) {
	do {
		const unsigned char repeat = *rle;
		if (repeat) {
			if (raster + repeat > raster_limit) {
				return raster;
			}
			const unsigned char val = *(rle + 1);
			for (int m = 0; m < repeat; ++m) {
				if ((m % 2) == 0) {
					*raster = val >> 4;
				} else {
					*raster = val & 0x0f;
				}
				++raster;
			}
			rle += 2;
		} else {
			const enum bmp_rle_marker marker = *(rle + 1);
			switch (marker) {
			case end_of_scan_line:
				raster += (size_t)(raster_limit - raster) % scan_len;
				rle += 2;
				break;
			case end_of_rle:
				return raster;
			case delta:
				if (rle + 3 > rle_limit) {
					return raster;
				}
				const size_t x_diff = *(rle + 2);
				const size_t y_diff = *(rle + 3);
				raster += x_diff + y_diff * scan_len;
				rle += 4;
				break;
			default:
				rle += 2;
				const size_t lone_nibble = marker % 2;
				const size_t bytes = (marker + 1) / 2;
				const size_t pad = bytes % 2 + lone_nibble;
				if (raster + marker > raster_limit
				|| rle + bytes + pad > rle_limit) {
					return raster;
				}
				for (size_t m = 0; m < marker; ++m) {
					if ((m & 1) == 0) {
						*raster = *rle >> 4;
					} else {
						*raster = *rle & 0x0f;
						++rle;
					}
					++raster;
				}
				rle += pad;
			}
		}
	} while (raster < raster_limit && rle < rle_limit);
	return raster;
}

static unsigned char * rle4_decode(const struct bmp_desc *desc,
unsigned char *rle) {
	// Note: We output in 8-bits.
	const size_t row = scanline_length(desc->w, 8, 4);
	const size_t raster_len = row * desc->h;
	unsigned char *raster = malloc(raster_len);
	if (!raster) {
		free(rle);
		return NULL;
	}

	const size_t rle_len = desc->data_len - desc->data_len % 1;
	unsigned char *raster_end = rle_loop4(raster,
		raster + raster_len, rle, rle + rle_len, row);
	free(rle);

	check_decode(raster_end, raster + raster_len);

	if (desc->pal) {
		const size_t out_len = desc->w * desc->h * 3;
		unsigned char *output = malloc(out_len);
		if (output) {
			strip_colormap_rgb8(output, raster, desc->pal, desc->w,
				desc->h, 8);
		}
		free(raster);
		return output;
	} else {
		return raster;
	}
}

static unsigned char * rle_loop8(unsigned char *restrict raster,
const unsigned char *restrict raster_limit, unsigned char *restrict rle,
const unsigned char *restrict rle_limit, const size_t scan_len) {
	do {
		const unsigned char repeat = *rle;
		if (repeat) {
			if (raster + repeat > raster_limit) {
				return raster;
			}
			const unsigned char val = *(rle + 1);
			memset(raster, val, repeat);
			raster += repeat;
			rle += 2;
		} else {
			const enum bmp_rle_marker marker = *(rle + 1);
			switch (marker) {
			case end_of_scan_line:
				raster += (size_t)(raster_limit - raster)
					% scan_len;
				rle += 2;
				break;
			case end_of_rle:
				return raster;
			case delta:
				if (rle + 3 > rle_limit) {
					return raster;
				}
				const unsigned char x_diff = *(rle + 2);
				const unsigned char y_diff = *(rle + 3);
				raster += x_diff + y_diff * scan_len;
				rle += 4;
				break;
			default:
				rle += 2;
				if (raster + marker > raster_limit
				|| rle + marker > rle_limit) {
					return raster;
				}
				memcpy(raster, rle, marker);
				raster += marker;
				rle += marker + marker % 2;
			}
		}
	} while (raster < raster_limit && rle < rle_limit);
	return raster;
}

static unsigned char * rle8_decode(const struct bmp_desc *desc,
unsigned char *rle) {
	const size_t raster_len = desc->scan_len * desc->h;
	unsigned char *raster = malloc(raster_len);
	if (!raster) {
		free(rle);
		return NULL;
	}

	unsigned char *raster_end = rle_loop8(raster, raster + raster_len, rle,
		rle + desc->data_len, desc->scan_len);

	free(rle);
	check_decode(raster_end, raster + raster_len);
	return uncompressed_decode(desc, raster);
}

unsigned char * bmp_decode(const struct bmp_desc *desc) {
	unsigned char *data = malloc(desc->data_len);
	if (!data) {
		return NULL;
	}

	const size_t read = fread(data, 1, desc->data_len, desc->ifp);
	if (read != desc->data_len) {
		puts("BMP warning: Got unexpected End of File while reading "
			"bitmap data. Output may contain garbage.");
	}

	switch (desc->compression) {
	case bmp_no_compression:
		return uncompressed_decode(desc, data);
	case bmp_8bit_rle:
		return rle8_decode(desc, data);
	case bmp_4bit_rle:
		return rle4_decode(desc, data);
	case bmp_mask:
		puts("Mask compression unsupported (for now)");
		break;
	}
	free(data);
	return NULL;
}

struct colormap * bmp_take_colormap(struct bmp_desc *desc) {
	struct colormap *m = desc->pal;
	desc->pal = NULL;
	return m;
}

static bool validate_file_size(struct bmp_desc *desc) {
	const long start = ftell(desc->ifp);
	fseek(desc->ifp, 0, SEEK_END);
	const long end = ftell(desc->ifp);
	const size_t file_size = (size_t)(end - start);

	const size_t raster_len = desc->scan_len * desc->h;
	if (desc->compression == bmp_8bit_rle
	|| desc->compression == bmp_4bit_rle) {
		/* E.g. 0x00 0x03 0xff 0xff 0xff 0x00... -> 0xff 0xff 0xff...
		 * This is ignoring the obvious 0x00 0x02 0x00 0x00,
		 * but why would anyone spam that one? */
		const size_t pathological_rle = raster_len * 2;
		desc->data_len = zumin(file_size, pathological_rle);
		if (desc->data_len % 2) {
			puts("BMP warning: RLE data may be incomplete.");
		}
	} else {
		if (file_size < raster_len) {
			return false;
		}
		desc->data_len = raster_len;
	}

	fseek(desc->ifp, start, SEEK_SET);
	return true;
}


static enum bmp_fail load_mask(struct bmp_desc *desc) {
	const size_t len = desc->type == bmp_type3 ? 3 : 4;
	u_int32_t buf[4];
	const size_t read = fread(buf, sizeof(*buf), len, desc->ifp);
	if (read != len) {
		return bmp_unexpected_eof;
	}

	// Image data is big-endian, but masks are stored as little-endian,
	if (desc->type == bmp_type3) {
		desc->mask.r = endian_u32(buf, big_endian);
		desc->mask.g = endian_u32(buf + 1, big_endian);
		desc->mask.b = endian_u32(buf + 2, big_endian);
	} else {
		desc->mask.r = endian_u32(buf, big_endian);
		desc->mask.g = endian_u32(buf + 1, big_endian);
		desc->mask.b = endian_u32(buf + 2, big_endian);
		desc->mask.a = endian_u32(buf + 3, big_endian);
	}

	return bmp_ok;
}

static enum bmp_fail load_palette(struct bmp_desc *desc) {
	// BMP type 2 entries are BGR, while type >= 3 are BGR0
	const size_t entry_len = desc->type == bmp_type2 ? 3 : 4;
	const size_t entries = 1U << desc->bitdepth;
	desc->pal = malloc(sizeof(*desc->pal) * entries);
	if (!desc->pal) {
		return bmp_alloc_error;
	}

	/* Palettes may be smaller than 1U << bitdepth, but the calculation is
	 * quite tedious so we'll just pretend that doesn't happen and fill the
	 * palette with whatever garbage we get. */
	fread(desc->pal, entry_len, entries, desc->ifp);

	if (entry_len == 3) {
		unsigned char *palette = (unsigned char *)desc->pal;
		for (size_t i = entries; i > 0; --i) {
			memmove(palette + i*4, palette + i*3, 3);
		}
	}

	return bmp_ok;
}

static enum bmp_fail validate_bitmap_header(struct bmp_desc *desc,
const int32_t width, const int32_t height, const u_int16_t planes,
const u_int16_t depth, const u_int32_t compression) {
	if (width > 0) {
		desc->w = (u_int32_t)width;
	} else {
		return bmp_invalid_header;
	}

	if (height > 0) {
		desc->h = (u_int32_t)height;
		desc->order = bmp_bottom_up;
	} else if (height < 0) {
		desc->h = (u_int32_t)(-height);
		desc->order = bmp_top_down;
	} else {
		return bmp_invalid_header;
	}

	if (planes != 1) {
		return bmp_unsupported_format;
	}

	switch (depth) {
	case 1: case 2: case 4: case 8: case 24:
		break;
	case 16: case 32:
		return bmp_unsupported_format;
	default:
		return bmp_invalid_header;
	}

	switch (compression) {
	case bmp_8bit_rle:
		if (depth != 8 || desc->order == bmp_top_down) {
			return bmp_invalid_header;
		}
		break;
	case bmp_4bit_rle:
		if (depth != 4 || desc->order == bmp_top_down) {
			return bmp_invalid_header;
		}
		break;
	case bmp_mask:
		if (depth != 16 || depth != 32) {
			return bmp_invalid_header;
		}
		break;
	case bmp_no_compression:
		break;
	default:
		return bmp_invalid_header;
	}

	desc->bitdepth = (unsigned char)depth;
	desc->compression = (unsigned char)compression;
	return bmp_ok;
}

static enum bmp_fail bmp_type3_parse_header(struct bmp_desc *desc) {
	/* Type 3 and NT BMP header (after header size)
	 * Bitmap header:
		Offset  Size    Name
		0       LONG    Width;          // Width in pixels
		4       LONG    Height;         // Height in pixels
		8       WORD    Planes;         // Nr of color planes (always 1)
		10      WORD    BitsPerPixel;
		12      DWORD   Compression;    // Compression method
		16      DWORD   SizeOfBitmap;   // Size of bitmap in bytes
		20      LONG    HorzResolution; // In pixels per meter
		24      LONG    VertResolution; // In pixels per meter
		28      DWORD   ColorsUsed;     // Number of colors in the image
		32      DWORD   ColorsImportant;// Number of important colors
		36                              // RGB0 Palette or RGB mask

	 * Type 4 additional fields:
		Offset  Size    Name
		36      DWORD   RedMask;
		40      DWORD   GreenMask;
		44      DWORD   BlueMask;
		48      DWORD   AlphaMask;
		52      DWORD   CSType;         // Color space
		56      LONG    RedX;           // X coord of red endpoint
		60      LONG    RedY;
		64      LONG    RedZ;
		68      LONG    GreenX;
		72      LONG    GreenY;
		76      LONG    GreenZ;
		80      LONG    BlueX;
		84      LONG    BlueY;
		88      LONG    BlueZ;
		92      DWORD   GammaRed;       // Gamma red coord scale value
		96      DWORD   GammaGreen;
		100     DWORD   GammaBlue;
		104
	 */

	u_int8_t buf[16];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return bmp_unexpected_eof;
	}

	const enum bmp_fail status = validate_bitmap_header(desc,
		(int32_t)endian_u32(buf, little_endian),
		(int32_t)endian_u32(buf + 4, little_endian),
		endian_u16(buf + 8, little_endian),
		endian_u16(buf + 10, little_endian),
		endian_u32(buf + 12, little_endian));
	if (status != bmp_ok) {
		return status;
	}

	desc->scan_len = scanline_length(desc->w, desc->bitdepth, 4);
	long ignored;
	if (desc->compression == bmp_mask) {
		ignored = 20;
	} else {
		ignored = desc->type - (long)sizeof(buf) - 4;
	}

	if (desc->bitdepth <= 8) {
		fseek(desc->ifp, ignored, SEEK_CUR);
		return load_palette(desc);
	} else if (desc->bitdepth != 24) {
		if (desc->compression == bmp_mask) {
			fseek(desc->ifp, ignored, SEEK_CUR);
			return load_mask(desc);
		}
		return bmp_invalid_header;
	}
	return bmp_ok;
}

static enum bmp_fail bmp_type2_parse_header(struct bmp_desc *desc) {
	/* Type 2 BMP header (after header size)
	 * Bitmap header:
		Offset  Size    Name
		0       SHORT   Width;          // Image width in pixels
		2       SHORT   Height;         // Image height in pixels
		4       WORD    Planes;         // Nr of color planes // Always 1
		6       WORD    BitsPerPixel;   // Nr of bits per pixel
		8
	 */

	u_int8_t buf[8];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return bmp_unexpected_eof;
	}

	const enum bmp_fail status = validate_bitmap_header(desc,
		(int16_t)endian_u16(buf, little_endian),
		(int16_t)endian_u16(buf + 2, little_endian),
		endian_u16(buf + 4, little_endian),
		endian_u16(buf + 6, little_endian),
		bmp_no_compression);
	if (status != bmp_ok) {
		return status;
	}

	desc->scan_len = scanline_length(desc->w, desc->bitdepth, 4);
	if (desc->bitdepth <= 8) {
		return load_palette(desc);
	} else if (desc->bitdepth != 24) {
		return bmp_invalid_header;
	}
	return bmp_ok;
}

enum bmp_fail bmp_parse_header(struct bmp_desc *desc) {
	/* Minimum non-type-1 BMP header (after magic bytes)
	 * File header:
		Offset	Size    Name
		0       DWORD   FileSize;	// Size of the file in bytes
		4       WORD    Reserved1;	// Always 0
		6       WORD    Reserved2;	// Always 0
		8       DWORD   BitmapOffset;	// Start pos of bitmap in bytes

	 * Bitmap header:
		Offset  Size    Name
		12      DWORD   Size;           // Size of this header in bytes
		16

	*/

	u_int32_t buf[4];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return bmp_unexpected_eof;
	}

//	const u_int32_t file_size = endian_u32(buf, little_endian);
	const u_int32_t bitmap_offset = endian_u32(buf + 2, little_endian);
	const u_int32_t header_size = endian_u32(buf + 3, little_endian);

	enum bmp_fail status;
	switch (header_size) {
	case bmp_type2:
		desc->type = (enum bmp_fail)header_size;
		status = bmp_type2_parse_header(desc);
		break;
	case bmp_type3:
	case bmp_type4:
	case bmp_type5:
		desc->type = (enum bmp_fail)header_size;
		status = bmp_type3_parse_header(desc);
		break;
	default:
		return bmp_unsupported_format;
	}

	if (status == bmp_ok) {
		fseek(desc->ifp, bitmap_offset, SEEK_SET);
		if (!validate_file_size(desc)) {
			return bmp_unexpected_eof;
		}
	}
	return status;
}

enum bmp_fail bmp_open_file(FILE *ifp, struct bmp_desc *desc) {
	enum bmp_fail status;
	unsigned char sig[2];
	if (fscanf(ifp, "%2c", sig) == 1) {
		if (!memcmp("BM", sig, sizeof(sig))) {
			desc->ifp = ifp;
			desc->pal = NULL;
			return bmp_ok;
		} else if (sig[0] == 0 && sig[1] == 0) {
			status = bmp_unsupported_format;
		} else {
			status = bmp_invalid_signature;
		}
	} else {
		status = bmp_unexpected_eof;
	}
	return status;
}
