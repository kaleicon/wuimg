#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#include "../common.h"
#include "../raster/raster.h"
#include "bmp.h"

const uint32_t BITFIELD_SHIFT = 16;

const char * bmp_compression_str(const enum bmp_compression comp) {
	switch (comp) {
	case bmp_no_compression: return "None";
	case bmp_8bit_rle: return "8bit RLE";
	case bmp_4bit_rle: return "4bit RLE";
	case bmp_bitfield: return "Bitfield";
	}
	return "???";
}

const char * bmp_type_str(const enum bmp_type type) {
	switch (type) {
	case bmp_type2: return "BITMAPCOREHEADER";
	case bmp_info_header: return "BITMAPINFOHEADER";
	case bmp_v2_info_header: return "BITMAPV2INFOHEADER";
	case bmp_v3_info_header: return "BITMAPV3INFOHEADER";
	case bmp_v4_header: return "BITMAPV4HEADER";
	case bmp_v5_header: return "BITMAPV5HEADER";
	}
	return "Unknown type";
}

void bmp_cleanup(struct bmp_desc *desc) {
	raster_free(&desc->r);
}

static void check_decode(const size_t end, const size_t expected_end) {
	if ( ((end + 3) & ~0x3U) < expected_end) {
		puts(RASTER_EOF);
	}
}

static size_t rle_loop4(unsigned char *restrict raster, const size_t raster_len,
unsigned char *restrict rle, const size_t rle_len, const size_t scan_len) {
	size_t i = 0;
	size_t o = 0;
	do {
		const unsigned char repeat = rle[i];
		if (repeat) {
			if (o + repeat > raster_len) {
				return o;
			}
			const unsigned char val = rle[i + 1];
			for (int m = 0; m < repeat; ++m) {
				if ((m & 1) == 0) {
					raster[o] = val >> 4;
				} else {
					raster[o] = val & 0x0f;
				}
				++o;
			}
			i += 2;
		} else {
			const enum bmp_rle_marker marker = rle[i + 1];
			switch (marker) {
			case bmp_end_of_scan_line:
				o += (raster_len - o) % scan_len;
				i += 2;
				break;
			case bmp_end_of_rle:
				return o;
			case bmp_delta:
				if (i + 3 > rle_len) {
					return o;
				}
				const size_t x_diff = rle[i + 2];
				const size_t y_diff = rle[i + 3];
				o += x_diff + y_diff * scan_len;
				i += 4;
				break;
			default:
				i += 2;
				const size_t lone_nibble = marker % 2;
				const size_t rle_bytes = (marker + 1) / 2;
				const size_t pad = rle_bytes % 2 + lone_nibble;
				if (o + marker > raster_len
				|| i + rle_bytes + pad > rle_len) {
					return o;
				}
				for (unsigned m = 0; m < marker; ++m) {
					if ((m & 1) == 0) {
						raster[o] = rle[i] >> 4;
					} else {
						raster[o] = rle[i] & 0x0f;
						++i;
					}
					++o;
				}
				i += pad;
			}
		}
	} while (o < raster_len && i + 1 < rle_len);
	return o;
}

static size_t rle_loop8(unsigned char *restrict raster, const size_t raster_len,
unsigned char *restrict rle, const size_t rle_len, const size_t scan_len) {
	size_t i = 0;
	size_t o = 0;
	do {
		const unsigned char repeat = rle[i];
		if (repeat) {
			if (o + repeat > raster_len) {
				return o;
			}
			const unsigned char val = rle[i + 1];
			memset(raster + o, val, repeat);
			o += repeat;
			i += 2;
		} else {
			const enum bmp_rle_marker marker = rle[i + 1];
			switch (marker) {
			case bmp_end_of_scan_line:
				o += (raster_len - o) % scan_len;
				i += 2;
				break;
			case bmp_end_of_rle:
				return o;
			case bmp_delta:
				if (i + 3 > rle_len) {
					return o;
				}
				;
				const unsigned char x_diff = rle[i + 2];
				const unsigned char y_diff = rle[i + 3];
				o += x_diff + y_diff * scan_len;
				i += 4;
				break;
			default:
				i += 2;
				if (o + marker > raster_len
				|| i + marker > rle_len) {
					return o;
				}
				memcpy(raster + o, rle + i, marker);
				o += marker;
				i += marker + marker % 2;
			}
		}
	} while (o < raster_len && i + 1 < rle_len);
	return o;
}

static unsigned char * rle_decode(const struct bmp_desc *desc,
unsigned char *restrict rle) {
	const size_t row = scanline_length(desc->r.w, 8, 4);
	const size_t raster_len = row * desc->r.h;
	unsigned char *raster = malloc(raster_len);
	if (!raster) {
		free(rle);
		return NULL;
	}

	const size_t written = (desc->compression == bmp_8bit_rle)
		? rle_loop8(raster, raster_len, rle, desc->data_len, row)
		: rle_loop4(raster, raster_len, rle, desc->data_len, row);

	free(rle);
	check_decode(written, raster_len);
	return raster;
}

static uint32_t expand_bits(const uint32_t word, const struct bmp_bitfield *p) {
	return (((word >> p->shift) & p->mask) * p->scale) >> BITFIELD_SHIFT;
}

static unsigned char * bitfield_decode(const struct bmp_desc *desc,
uint8_t *restrict data) {
	const size_t bytes = (desc->r.bitdepth > 8) ? 2 : 1;
	const size_t ch = (desc->r.ch == 4) ? 4 : 3; // loop unroll
	void *out = malloc(desc->r.w * desc->r.h * ch * bytes);
	if (!out) {
		free(data);
		return NULL;
	}

	const size_t stride = scanline_length(desc->r.w, desc->depth, 4);
	for (size_t y = 0; y < desc->r.h; ++y) {
		const uint8_t *d = data + y*stride;
		for (size_t x = 0; x < desc->r.w; ++x) {
			const uint32_t word = (desc->depth == 32)
				? endian32(((uint32_t *)d)[x], little_endian)
				: endian16(((uint16_t *)d)[x], little_endian);

			const size_t o = y * desc->r.w + x;
			for (size_t k = 0; k < ch; ++k) {
				const uint32_t val = expand_bits(word, desc->bf + k);
				if (bytes == 2) {
					((uint16_t *)out)[o*ch + k] = (uint16_t)val;
				} else {
					((uint8_t *)out)[o*ch + k] = (uint8_t)val;
				}
			}
		}
	}
	free(data);
	return out;
}

unsigned char * bmp_decode(const struct bmp_desc *desc) {
	void *data = malloc(desc->data_len);
	if (!data) {
		return NULL;
	}

	const size_t read = fread(data, 1, desc->data_len, desc->ifp);
	if (read != desc->data_len) {
		puts(RASTER_EOF);
	}

	switch (desc->compression) {
	case bmp_no_compression:
		break;
	case bmp_8bit_rle:
	case bmp_4bit_rle:
		return rle_decode(desc, data);
	case bmp_bitfield:
		return bitfield_decode(desc, data);
	}
	return data;
}

static bool validate_file_size(struct bmp_desc *desc) {
	const size_t file_size = (size_t)file_get_remaining(desc->ifp);

	const size_t raster_len = scanline_length(desc->r.w, desc->depth, 4)
		* desc->r.h;
	if (desc->compression == bmp_8bit_rle
	|| desc->compression == bmp_4bit_rle) {
		/* E.g. 0x00 0x03 0xff 0xff 0xff 0x00... -> 0xff 0xff 0xff...
		 * This is ignoring the obvious infinite 0x00 0x02 0x00 0x00 */
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
	return true;
}

static enum lib_fail load_mask(struct bmp_desc *desc) {
	uint8_t ch = desc->type < bmp_v3_info_header ? 3 : 4;
	uint32_t buf[4];
	if (fread(buf, sizeof(*buf), ch, desc->ifp) != ch) {
		return lib_unexpected_eof;
	}
	if (ch == 4 && !buf[3]) {
		ch = 3;
	}

	loop_endian32(buf, little_endian, ch);
	// Swizzling is free, so switch mask to BGRA order for consistency
	const uint32_t t = buf[0];
	buf[0] = buf[2];
	buf[2] = t;

	struct bmp_bitfield *bf = desc->bf;
	uint32_t xor_acc = 0;
	unsigned maxdepth = 0;
	for (int i = 0; i < ch; ++i) {
		uint32_t rem = buf[i];
		if (!rem) {
			bf[i] = (struct bmp_bitfield){0};
			continue;
		}

		unsigned zeroes = 0;
		while ((rem & 1) == 0) {
			rem >>= 1;
			++zeroes;
		}

		bf[i].shift = zeroes;
		bf[i].mask = rem;

		unsigned ones = 0;
		while ((rem & 1) != 0) {
			rem >>= 1;
			++ones;
		}

		// Mask bits must be continous and non-overlapping
		if (rem || (xor_acc & buf[i]) != 0) {
			return lib_invalid_header;
		}
		xor_acc ^= buf[i];

		if (ones > maxdepth) {
			maxdepth = ones;
		}
	}
	if (maxdepth > desc->depth / 2) {
		return lib_invalid_header;
	}

	const bool high_depth = maxdepth > 8;
	const unsigned target = high_depth ? USHRT_MAX : UCHAR_MAX;
	for (int i = 0; i < ch; ++i) {
		if (bf[i].mask) {
			bf[i].scale = (target << BITFIELD_SHIFT) / bf[i].mask + 1;
		}
	}

	desc->r.ch = ch;
	desc->r.bitdepth = high_depth ? 16 : 8;
	desc->r.alignment = 1;
	return lib_ok;
}

static enum lib_fail load_palette(struct bmp_desc *desc) {
	/* Palettes may be smaller than 1U << bitdepth, but handling every case
	 * is a bother plus we fseek to where the bitmap is, so we'll just
	 * pretend they are always the same and read whatever garbage we get. */
	const size_t entries = 1U << desc->depth;

	// BMP type 2 entries are BGR, while type >= 3 are BGR0
	const enum lib_pal type = (desc->type == bmp_type2)
		? lib_pal_rgb : lib_pal_rgbx;
	const enum lib_fail fail = lib_load_pal(desc->ifp, &desc->r.palette,
		type, entries);
	if (fail == lib_alloc_error) {
		return lib_alloc_error;
	}
	return lib_ok;
}

static enum lib_fail validate_bitmap_header(struct bmp_desc *desc,
const int32_t width, const int32_t height, const uint16_t planes,
const uint16_t depth, const uint32_t compression) {
	if (width < 1) {
		return lib_invalid_header;
	}
	if (!height) {
		return lib_invalid_header;
	}
	desc->r.w = (unsigned)width;
	desc->r.h = (unsigned)(height > 0 ? height : -height);
	desc->order = (height > 0) ? bmp_bottom_up : bmp_top_down;

	if (planes != 1) {
		return lib_unsupported_format;
	}

	switch (depth) {
	case 1: case 2: case 4: case 8:
		desc->r.ch = 1;
		desc->r.bitdepth = (unsigned char)depth;
		break;
	case 16: case 24: case 32:
		desc->r.ch = (unsigned char)(depth / 8);
		desc->r.bitdepth = 8;
		break;
	default:
		return lib_invalid_header;
	}

	switch (compression) {
	case bmp_no_compression:
		if (depth == 16) {
			desc->r.attr = pix_packing_1555;
		}
		break;
	case bmp_8bit_rle:
		if (depth != 8 || desc->order == bmp_top_down) {
			return lib_invalid_header;
		}
		break;
	case bmp_4bit_rle:
		if (depth != 4 || desc->order == bmp_top_down) {
			return lib_invalid_header;
		}
		desc->r.bitdepth = 8; // Our algorithm decodes to 8bit
		break;
	case bmp_bitfield:
		if (depth != 16 && depth != 32) {
			return lib_invalid_header;
		}
		break;
	default:
		return lib_invalid_header;
	}

	desc->depth = (unsigned char)depth;
	desc->compression = (unsigned char)compression;
	return lib_ok;
}

static enum lib_fail bmp_type3_parse_header(struct bmp_desc *desc) {
	/* Type 3 and up DIB header (after header size field).

	 * BITMAPINFOHEADER:
		Offset  Size    Name
		0       LONG    Width           // Width in pixels
		4       LONG    Height          // Height in pixels
		8       WORD    Planes          // Nr of color planes (always 1)
		10      WORD    BitsPerPixel
		12      DWORD   Compression     // Compression method
		16      DWORD   SizeOfBitmap    // Size of bitmap in bytes
		20      LONG    HorzResolution  // In pixels per meter
		24      LONG    VertResolution  // In pixels per meter
		28      DWORD   ColorsUsed      // Number of colors in the image
		32      DWORD   ColorsImportant // Number of important colors
		36

	 * Additional fields when Compression == 3 or when BITMAPV2INFOHEADER
	 * is used:
		Offset  Size    Name
		36      DWORD   RedMask
		40      DWORD   GreenMask
		44      DWORD   BlueMask
		48

	 * BITMAPV3INFOHEADER additional field:
		Offset  Size    Name
		48      DWORD   AlphaMask
		52

	 * BITMAPV4HEADER additional fields:
		Offset  Size    Name
		52      DWORD   ColorSpaceType
		56      LONG    RedX            // X coord of red endpoint
		60      LONG    RedY
		64      LONG    RedZ
		68      LONG    GreenX
		72      LONG    GreenY
		76      LONG    GreenZ
		80      LONG    BlueX
		84      LONG    BlueY
		88      LONG    BlueZ
		92      DWORD   GammaRed        // Gamma red coord scale value
		96      DWORD   GammaGreen
		100     DWORD   GammaBlue
		104

	 * BITMAPV5HEADER additional fields:
		Offset  Size    Name
		104     DWORD   RenderingIntent
		108     DWORD   ProfileData
		112     DWORD   ProfileSize
		116     DWORD   Reserved
		120

	 * Afterwards comes the palette.
	 */

	uint8_t buf[16];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return lib_unexpected_eof;
	}

	const enum lib_fail fail = validate_bitmap_header(desc,
		(int32_t)buf_endian32(buf, little_endian),
		(int32_t)buf_endian32(buf + 4, little_endian),
		buf_endian16(buf + 8, little_endian),
		buf_endian16(buf + 10, little_endian),
		buf_endian32(buf + 12, little_endian));
	if (fail) {
		return fail;
	}

	if (desc->compression == bmp_bitfield) {
		fseek(desc->ifp, 20, SEEK_CUR);
		return load_mask(desc);
	} else if (desc->depth <= 8) {
		const long header_skip = desc->type - (long)sizeof(buf) - 4;
		fseek(desc->ifp, header_skip, SEEK_CUR);
		return load_palette(desc);
	}
	return lib_ok;
}

static enum lib_fail bmp_type2_parse_header(struct bmp_desc *desc) {
	/* Type 2 DIB header (after header size)
	 * Bitmap header:
		Offset  Size    Name
		0       SHORT   Width           // Image width in pixels
		2       SHORT   Height          // Image height in pixels
		4       WORD    Planes          // Nr of color planes. Always 1
		6       WORD    BitsPerPixel    // Nr of bits per pixel
		8
	 */

	uint16_t buf[4];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return lib_unexpected_eof;
	}

	const enum lib_fail fail = validate_bitmap_header(desc,
		(int16_t)endian16(buf[0], little_endian),
		(int16_t)endian16(buf[1], little_endian),
		endian16(buf[2], little_endian),
		endian16(buf[3], little_endian),
		bmp_no_compression);
	if (fail) {
		return fail;
	}

	if (desc->depth <= 8) {
		return load_palette(desc);
	} else if (desc->depth != 24) {
		return lib_invalid_header;
	}
	return lib_ok;
}

enum lib_fail bmp_parse_header(struct bmp_desc *desc) {
	/* Minimum non-type-1 BMP header (after magic bytes)
	 * File header:
		Offset	Size    Name
		0       DWORD   FileSize     // Size of the file in bytes
		4       WORD    Reserved1
		6       WORD    Reserved2
		8       DWORD   BitmapOffset // Start pos of bitmap in bytes

	 * Common DIB header:
		Offset  Size    Name
		12      DWORD   Size         // Size of the DIB header in bytes
		16

	*/

	uint32_t buf[4];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return lib_unexpected_eof;
	}

//	const uint32_t file_size = endian32(buf[0], little_endian);
	const uint32_t bitmap_offset = endian32(buf[2], little_endian);
	const enum bmp_type header_size = endian32(buf[3], little_endian);

	desc->r = (struct raster_desc){
		.alignment = 4,
		.layout = pix_bgra,
	};
	enum lib_fail status;
	switch (header_size) {
	case bmp_type2:
		desc->type = header_size;
		status = bmp_type2_parse_header(desc);
		break;
	case bmp_info_header:
	case bmp_v2_info_header:
	case bmp_v3_info_header:
	case bmp_v4_header:
	case bmp_v5_header:
		desc->type = header_size;
		status = bmp_type3_parse_header(desc);
		break;
	default:
		return lib_unsupported_format;
	}

	if (status == lib_ok) {
		raster_normalize(&desc->r);
		fseek(desc->ifp, bitmap_offset, SEEK_SET);
		if (!validate_file_size(desc)) {
			return lib_unexpected_eof;
		}
	}
	return status;
}

enum lib_fail bmp_open_file(FILE *ifp, struct bmp_desc *desc) {
	enum lib_fail fail;
	unsigned char sig[2];
	if (fread(sig, 1, sizeof(sig), ifp) == sizeof(sig)) {
		if (!memcmp("BM", sig, sizeof(sig))) {
			desc->ifp = ifp;
			return lib_ok;
		} else if (sig[0] == 0 && sig[1] == 0) { // DDB
			fail = lib_unsupported_format;
		} else {
			fail = lib_invalid_signature;
		}
	} else {
		fail = lib_unexpected_eof;
	}
	return fail;
}
