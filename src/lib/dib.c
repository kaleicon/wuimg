#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#include "../common.h"
#include "../raster/bit.h"
#include "../raster/raster.h"
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

const char * dib_compression_str(const enum dib_compression comp) {
	switch (comp) {
	case dib_no_compression: return "None";
	case dib_8bit_rle: return "8bit RLE";
	case dib_4bit_rle: return "4bit RLE";
	case dib_bitfield: return "Bitfield";
	}
	return "???";
}

const char * dib_type_str(const enum dib_type type) {
	switch (type) {
	case dib_core_header: return "BITMAPCOREHEADER";
	case dib_info_header: return "BITMAPINFOHEADER";
	case dib_v2_info_header: return "BITMAPV2INFOHEADER";
	case dib_v3_info_header: return "BITMAPV3INFOHEADER";
	case dib_v4_header: return "BITMAPV4HEADER";
	case dib_v5_header: return "BITMAPV5HEADER";
	}
	return "???";
}

void dib_cleanup(struct dib_desc *desc) {
	raster_free(&desc->r);
}

static void check_decode(const size_t end, const size_t expected_end) {
	if ( ((end + 3) & ~0x3U) < expected_end) {
		puts(RASTER_EOF);
	}
}

static size_t rle_loop4(unsigned char *restrict raster, const size_t raster_len,
const unsigned char *restrict rle, const size_t rle_len, const size_t scan_len) {
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
			const enum dib_rle_marker marker = rle[i + 1];
			switch (marker) {
			case dib_end_of_scan_line:
				o += (raster_len - o) % scan_len;
				i += 2;
				break;
			case dib_end_of_rle:
				return o;
			case dib_delta:
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
			const enum dib_rle_marker marker = rle[i + 1];
			switch (marker) {
			case dib_end_of_scan_line:
				o += (raster_len - o) % scan_len;
				i += 2;
				break;
			case dib_end_of_rle:
				return o;
			case dib_delta:
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

static unsigned char * rle_decode(const struct dib_desc *desc,
unsigned char *restrict rle) {
	const size_t row = scanline_length(desc->r.w, 8, 4);
	const size_t raster_len = row * desc->r.h;
	unsigned char *raster = malloc(raster_len);
	if (!raster) {
		free(rle);
		return NULL;
	}

	const size_t written = (desc->compression == dib_8bit_rle)
		? rle_loop8(raster, raster_len, rle, desc->size, row)
		: rle_loop4(raster, raster_len, rle, desc->size, row);

	free(rle);
	check_decode(written, raster_len);
	return raster;
}

static uint32_t expand_bits(const uint32_t word, const struct dib_bitfield *p) {
	return (((word >> p->shift) & p->mask) * p->scale) >> BITFIELD_SHIFT;
}

static unsigned char * bitfield_decode(const struct dib_desc *desc,
uint8_t *restrict data) {
	const size_t bytes = (desc->r.bitdepth > 8) ? 2 : 1;
	const size_t ch = (desc->r.ch == 4) ? 4 : 3; // putting both helps perf.
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

unsigned char * dib_decode(const struct dib_desc *desc) {
	void *data = malloc(desc->size);
	if (!data) {
		return NULL;
	}

	const size_t read = fread(data, 1, desc->size, desc->ifp);
	if (read != desc->size) {
		puts(RASTER_EOF);
	}

	switch (desc->compression) {
	case dib_no_compression:
		if (desc->depth == 16) {
			lib_raster_endian(data, &desc->r, little_endian);
		}
		break;
	case dib_8bit_rle:
	case dib_4bit_rle:
		return rle_decode(desc, data);
	case dib_bitfield:
		return bitfield_decode(desc, data);
	}
	return data;
}

static enum lib_fail load_mask(struct dib_desc *desc) {
	uint8_t ch = desc->type < dib_v3_info_header ? 3 : 4;
	uint32_t buf[4];
	if (fread(buf, sizeof(*buf), ch, desc->ifp) != ch) {
		return lib_unexpected_eof;
	}
	if (desc->type > dib_v3_info_header) {
		fseek(desc->ifp, desc->type - dib_v3_info_header, SEEK_CUR);
	}

	if (ch == 4 && !buf[3]) {
		ch = 3;
	}

	loop_endian32(buf, little_endian, ch);
	// Swizzling is free, so switch mask to BGRA order for consistency
	const uint32_t t = buf[0];
	buf[0] = buf[2];
	buf[2] = t;

	struct dib_bitfield *bf = desc->bf;
	uint32_t xor_acc = 0;
	unsigned maxdepth = 0;
	for (int i = 0; i < ch; ++i) {
		uint32_t rem = buf[i];
		if (!rem) {
			bf[i] = (struct dib_bitfield){0};
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

static enum lib_fail load_pal(struct dib_desc *desc, const enum lib_pal pal_type) {
	return lib_load_pal(desc->ifp, &desc->r.palette, pal_type,
		desc->pal_entries);
}

static enum lib_fail validate_common(struct dib_desc *desc,
const uint16_t planes, const uint32_t colors) {
	if (planes > 1) { // Some files set it to 0
		return lib_invalid_header;
	}
	if (desc->depth <= 8) {
		if (colors > 256) {
			return lib_invalid_header;
		} else if (colors) {
			desc->pal_entries = colors;
		} else {
			desc->pal_entries = 1 << desc->depth;
		}
	}
	return lib_ok;
}

static enum lib_fail validate_os2_header(struct dib_desc *desc,
const uint32_t width, const uint32_t height, const uint16_t depth,
const uint32_t compression, const uint32_t size, const uint16_t storage,
const uint32_t color_encoding) {
	if (!width || !height) {
		return lib_invalid_header;
	}
	desc->r.w = width;
	desc->r.h = height;

	switch (depth) {
	case 1: case 4: case 8:
		desc->r.ch = 1;
		desc->r.bitdepth = (unsigned char)depth;
		break;
	case 24:
		desc->r.ch = 3;
		desc->r.bitdepth = 8;
		break;
	default:
		return lib_invalid_header;
	}

	switch (compression) {
	case os2_no_compression: break;
	case os2_8bit_rle:
		if (depth != 8 || size == 0) {
			return lib_invalid_header;
		}
		break;
	case os2_4bit_rle:
		if (depth != 4 || size == 0) {
			return lib_invalid_header;
		}
		break;
	case os2_1d_huffman:
		return lib_unsupported_feature;
	case os2_24bit_rle:
		if (depth != 24 || size == 0) {
			return lib_invalid_header;
		}
		break;
	}

	if (storage != 0) {
		return lib_invalid_header;
	}
	if (color_encoding != 0) {
		return lib_invalid_header;
	}
	desc->depth = (unsigned char)depth;
	desc->order = dib_bottom_up;
	desc->compression = (unsigned char)compression;
	desc->size = size;
	return lib_ok;
}

static enum lib_fail validate_dib_header(struct dib_desc *desc,
const int32_t width, const int32_t height, const uint16_t depth,
const uint32_t compression, const uint32_t size) {
	if (width < 1) {
		return lib_invalid_header;
	}
	if (!height) {
		return lib_invalid_header;
	}
	desc->r.w = (unsigned)width;
	desc->r.h = (unsigned)(height > 0 ? height : -height);
	desc->order = (height > 0) ? dib_bottom_up : dib_top_down;

	switch (depth) {
	case 2: /* Windows CE */
	case 1: case 4: case 8:
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
	case dib_no_compression:
		if (depth == 16) {
			desc->r.attr = pix_packing_1555;
		}
		break;
	case dib_8bit_rle:
		if (depth != 8 || desc->order == dib_top_down || size == 0) {
			return lib_invalid_header;
		}
		break;
	case dib_4bit_rle:
		if (depth != 4 || desc->order == dib_top_down || size == 0) {
			return lib_invalid_header;
		}
		desc->r.bitdepth = 8; // Our algorithm decodes to 8bit
		break;
	case dib_bitfield:
		if (depth != 16 && depth != 32) {
			return lib_invalid_header;
		}
		break;
	default:
		return lib_invalid_header;
	}
	desc->depth = (unsigned char)depth;
	desc->compression = (unsigned char)compression;
	desc->size = size;
	return lib_ok;
}

static enum lib_fail dib_parse_os2_v2_header(struct dib_desc *desc) {
	/* OS/2 v2 header (after header size field)
		Offset  Size    Name
		0       DWORD   Width           // Width in pixels
		4       DWORD   Height          // Height in pixels
		8       WORD    Planes          // Nr of color planes (always 1)
		10      WORD    BitsPerPixel
		12      DWORD   Compression     // Compression method
		16      DWORD   RLEBitmapSize
		20      DWORD   HorzResolution  // In 'Units'
		24      DWORD   VertResolution  // In 'Units'
		28      DWORD   ColorsUsed      // Nr of palette colors, or 0
		32      DWORD   ColorsImportant // Nr of important colors
		36      WORD    Units           // Always 0 (pixels per meter)
		38      WORD    Padding
		40      WORD    ScanlineStorage // Always 0 (left-to-right, bottom-up)
		42      WORD    HalftoneAlgorithm
		44      DWORD   HalftoneVar1
		48      DWORD   HalftoneVar2
		52      DWORD   ColorEncoding   // Always 0 (RGB)
		56      DWORD   Identifier      // Reserved for application use
		60

	 * If the header size value is less than 64, the missing values are
	 * assumed to be 0.
	*/

	uint8_t buf[60] = {0};
	const uint32_t rem = desc->type - 4;
	if (fread(buf, 1, rem, desc->ifp) != rem) {
		return lib_unexpected_eof;
	}

	enum lib_fail status = validate_os2_header(desc,
		buf_endian32(buf, little_endian),
		buf_endian32(buf + 4, little_endian),
		buf_endian16(buf + 10, little_endian),
		buf_endian32(buf + 12, little_endian),
		buf_endian32(buf + 16, little_endian),
		buf_endian16(buf + 40, little_endian),
		buf_endian32(buf + 52, little_endian));
	if (status != lib_ok) {
		return status;
	}
	status = validate_common(desc,
		buf_endian16(buf + 8, little_endian),
		buf_endian32(buf + 28, little_endian));
	if (status != lib_ok) {
		return status;
	}
	if (desc->depth <= 8) {
		return load_pal(desc, lib_pal_rgbx);
	}
	return lib_ok;
}

static bool valid_os2_v2(const uint32_t size) {
	if (size >= 16 && size <= 64) {
		return size % 4 == 0 || size == 14 || size == 42 || size == 46;
	}
	return false;
}

static enum lib_fail dib_parse_type3_header(struct dib_desc *desc) {
	/* Type 3 and up DIB header (after header size field)

	 * BITMAPINFOHEADER:
		Offset  Size    Name
		0       LONG    Width           // Width in pixels
		4       LONG    Height          // Height in pixels
		8       WORD    Planes          // Nr of color planes (always 1)
		10      WORD    BitsPerPixel
		12      DWORD   Compression     // Compression method
		16      DWORD   RLEBitmapSize
		20      LONG    HorzResolution  // In pixels per meter
		24      LONG    VertResolution  // In pixels per meter
		28      DWORD   ColorsUsed      // Nr of palette colors, or 0
		32      DWORD   ColorsImportant // Nr of important colors
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

	uint8_t buf[32];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	enum lib_fail status = validate_dib_header(desc,
		(int32_t)buf_endian32(buf, little_endian),
		(int32_t)buf_endian32(buf + 4, little_endian),
		buf_endian16(buf + 10, little_endian),
		buf_endian32(buf + 12, little_endian),
		buf_endian32(buf + 16, little_endian));
	if (status != lib_ok) {
		return status;
	}
	status = validate_common(desc,
		buf_endian16(buf + 8, little_endian),
		buf_endian32(buf + 28, little_endian));
	if (status != lib_ok) {
		return status;
	}

	const long header_pos = (long)sizeof(buf) + 4 /* Size field */;
	if (desc->compression == dib_bitfield) {
		fseek(desc->ifp, dib_info_header - header_pos, SEEK_CUR);
		return load_mask(desc);
	} else {
		fseek(desc->ifp, desc->type - header_pos, SEEK_CUR);
		if (desc->depth <= 8) {
			return load_pal(desc, lib_pal_rgbx);
		}
	}
	return lib_ok;
}

static enum lib_fail dib_parse_core_header(struct dib_desc *desc) {
	/* Type 2 DIB header (after header size)
		Offset  Size    Name
		0       SHORT   Width           // Image width in pixels
		2       SHORT   Height          // Image height in pixels
		4       WORD    Planes          // Nr of color planes. Always 1
		6       WORD    BitsPerPixel    // Nr of bits per pixel
		8

	 * For OS/2, width and height are unsigned. There's no reliable way of
	 * telling them apart.
	*/

	uint16_t buf[4];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	enum lib_fail status = validate_dib_header(desc,
		(int16_t)endian16(buf[0], little_endian),
		(int16_t)endian16(buf[1], little_endian),
		endian16(buf[3], little_endian),
		dib_no_compression, 0);
	if (status != lib_ok) {
		return status;
	}
	status = validate_common(desc, endian16(buf[2], little_endian), 0);
	if (status != lib_ok) {
		return status;
	}

	if (desc->depth <= 8) {
		return load_pal(desc, lib_pal_rgb);
	} else if (desc->depth != 24) {
		return lib_invalid_header;
	}
	return lib_ok;
}

static enum lib_fail dib_parse_header(struct dib_desc *desc) {
	/* Common DIB header:
		Offset  Size    Name
		0       DWORD   Size         // Size of DIB header in bytes
		4
	*/
	uint32_t hsize;
	if (!fread(&hsize, sizeof(hsize), 1, desc->ifp)) {
		return lib_unexpected_eof;
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
	default:
		if (desc->is_os2 == trit_false || !valid_os2_v2(hsize)) {
			return lib_invalid_header;
		}
		desc->is_os2 = trit_true;
		break;
	}

	desc->type = (enum dib_type)hsize;
	desc->r = (struct raster_desc){
		.alignment = 4,
		.layout = pix_bgra,
	};

	enum lib_fail status;
	if (core_header) {
		status = dib_parse_core_header(desc);
	} else if (desc->is_os2 == trit_true) {
		status = dib_parse_os2_v2_header(desc);
	} else {
		status = dib_parse_type3_header(desc);
	}
	if (status != lib_ok) {
		return status;
	}

	raster_normalize(&desc->r);
	const size_t size = scanline_length(desc->r.w, desc->depth, 4)
		* desc->r.h;
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
	return lib_ok;
}

enum lib_fail dib_open_file(struct dib_desc *desc, FILE *ifp) {
	desc->ifp = ifp;
	return dib_parse_header(desc);
}

enum lib_fail bmp_parse_header(struct dib_desc *desc) {
	/* Minimum non-type-1 BMP header (after magic bytes)

		Offset	Size    Name
		0       DWORD   FileSize     // In bytes. Usually 0
		4       WORD    XHotSpot     // Valid only for OS/2 icons and
		6       WORD    YHotSpot     //  pointers. Reserved for Windows
		8       DWORD   BitmapOffset // Start offset of bitmap in bytes
		12

	 * DIB header follows afterwards.
	*/

	uint32_t buf[3];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	enum lib_fail status = dib_parse_header(desc);
	if (status != lib_ok) {
		return status;
	}

	const long bitmap_offset = endian32(buf[2], little_endian);
	fseek(desc->ifp, bitmap_offset, SEEK_SET);
	return status;
}

enum lib_fail bmp_open_file(struct dib_desc *desc, FILE *ifp) {
	memset(desc, 0, sizeof(*desc));

	enum lib_fail fail;
	const unsigned char magic[][2] = {
		{'B', 'M'}, // BMP
		{0, 0}, // DDB
	};
	unsigned char sig[2];
	if (fread(sig, sizeof(sig), 1, ifp)) {
		if (!memcmp(magic[0], sig, sizeof(sig))) {
			desc->ifp = ifp;
			return lib_ok;
		} else if (!memcmp(magic[1], sig, sizeof(sig))) {
			fail = lib_unsupported_feature;
		} else {
			fail = lib_invalid_signature;
		}
	} else {
		fail = lib_unexpected_eof;
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
	dib_cleanup(&desc->dib);
}
__attribute__((unused))
static bool get_bit(const uint8_t *and, const size_t x) {
	return (and[x/8] >> (7 - (x % 8))) & 1;
}

static void ico_buf_sizes(struct ico_buf *buf, const struct dib_desc *dib,
const unsigned char depth) {
	buf->stride = scanline_length(dib->r.w, depth, 4);
	buf->size = buf->stride * dib->r.h;
}

static bool ico_buf_load(struct ico_buf *buf, const struct dib_desc *dib,
const unsigned char depth, FILE *ifp) {
	ico_buf_sizes(buf, dib, depth);
	buf->buf = malloc(buf->size);
	if (buf->buf) {
		return fread(buf->buf, 1, buf->size, ifp);
	}
	return false;
}

static void ico_32bit_dec(const struct dib_desc *dib, struct pix_rgba8 *dst,
const uint8_t *and, const size_t and_stride) {
	for (size_t y = 0; y < dib->r.h; ++y) {
		struct pix_rgba8 *d = dst + dib->r.w * y;
		const uint8_t *a = and + and_stride * y;
		for (size_t x = 0; x < dib->r.w; ++x) {
			if (bit_get(a, x)) {
				d[x].a = 0;
			}
		}
	}
}

static unsigned char * ico_word_dec(const struct dib_desc *dib) {
	struct ico_buf dst, and;
	if (!ico_buf_load(&dst, dib, dib->depth, dib->ifp)) {
		return NULL;
	}
	if (!ico_buf_load(&and, dib, 1, dib->ifp)) {
		free(dst.buf);
		return NULL;
	}

	if (dib->depth == 32) {
		ico_32bit_dec(dib, (struct pix_rgba8 *)dst.buf, and.buf,
			and.stride);
	} else {
		for (size_t y = 0; y < dib->r.h; ++y) {
			uint16_t *d = (uint16_t *)(dst.buf + dst.stride * y);
			const uint8_t *a = and.buf + and.stride * y;
			for (size_t x = 0; x < dib->r.w; ++x) {
				const bool bit = get_bit(a, x);
				int i = (endian16(d[x], little_endian) & 0x7fff)
					| (!bit << 15);
				d[x] = (uint16_t)i;
			}
		}
	}
	free(and.buf);
	return dst.buf;
}

static bool ico_truecolor_expands(const struct dib_desc *dib,
struct ico_buf *restrict dst, struct ico_buf *restrict xor,
struct ico_buf *restrict and) {
	ico_buf_sizes(dst, dib, 32);
	ico_buf_sizes(xor, dib, dib->depth);
	ico_buf_sizes(and, dib, 1);

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

static unsigned char * ico_24bit_dec(const struct dib_desc *dib) {
	struct ico_buf dst, xor, and;
	if (!ico_truecolor_expands(dib, &dst, &xor, &and)) {
		return NULL;
	}

	for (size_t y = 0; y < dib->r.h; ++y) {
		uint8_t *d = dst.buf + dst.stride * y;
		uint8_t *s = xor.buf + xor.stride * y;
		uint8_t *a = and.buf + and.stride * y;
		for (size_t x = 0; x < dib->r.w; ++x) {
			d[x*4] = s[x*3];
			d[x*4 + 1] = s[x*3 + 1];
			d[x*4 + 2] = s[x*3 + 2];
			d[x*4 + 3] = (get_bit(a, x) ? 0x00 : 0xff);
		}
	}
	free(xor.buf);
	return dst.buf;
}

static unsigned char *ico_palette_dec(struct dib_desc *dib) {
	struct ico_buf dst, xor, and;
	if (!ico_truecolor_expands(dib, &dst, &xor, &and)) {
		return NULL;
	}

	raster_pal_expand(dst.buf, xor.buf, dib->r.palette, dib->r.w, dib->r.h,
		4, 4, dib->depth);
	ico_32bit_dec(dib, (struct pix_rgba8 *)dst.buf, and.buf, and.stride);

	free(xor.buf);
	free(lib_raster_take_palette(&dib->r));
	return dst.buf;
}

unsigned char * ico_decode(struct ico_desc *desc) {
	struct dib_desc *dib = &desc->dib;
	switch (dib->depth) {
	case 16: case 32:
		return ico_word_dec(dib);
	case 24: return ico_24bit_dec(dib);
	}
	return ico_palette_dec(dib);
}

enum lib_fail ico_set_image(struct ico_desc *desc, const uint16_t i) {
	/* ICO image components:
		BITMAPINFOHEADER
		Palette
		XORMask
		ANDMask
	 * The image data is meant to be composited over a background, hence
	 * the Mask names. The AND mask sets whether the background is cleared
	 * first as in a AND operation (hence, it is the opposite of Alpha)
	 * while the XOR mask contains the normal image data. */
	struct dib_desc *dib = &desc->dib;
	fseek(dib->ifp, desc->images[i].offset, SEEK_SET);
	// dib_parse_header() will drop us at the start of the XOR bitmap.
	enum lib_fail status = dib_parse_header(dib);
	if (status != lib_ok) {
		return status;
	}

	// For bizarre reasons the XOR and AND bitmaps are counted together.
	if (dib->r.h % 2 != 0) {
		return lib_invalid_header;
	}
	dib->r.h /= 2;
	if (dib->depth != 16) {
		dib->r.bitdepth = 8;
		dib->r.ch = 4;
	}

	if (dib->type != dib_info_header
	|| dib->compression != dib_no_compression) {
		return lib_invalid_header;
	}
	return lib_ok;
}

enum lib_fail ico_parse_header(struct ico_desc *desc) {
	/* ICO dir entry (one for each image, stored continuously):
		Offset  Size    Name
		0       BYTE    Width
		1       BYTE    Height
		2       BYTE    ColorCount
		3       BYTE    Reserved     // Should be 0, but Windows ignores it
		4       WORD    Planes       // XHotspot for cursors
		6       WORD    BitsPerPixel // YHotspot for cursors
		8       DWORD   ImageSize
		12      DWORD   ImageOffset
		16
	*/

	desc->images = malloc(sizeof(*desc->images) * desc->count);
	if (!desc->images) {
		return lib_alloc_error;
	}

	for (uint16_t i = 0; i < desc->count; ++i) {
		/* Each image has a DIB header, so we only save the image
		 * location and verify the values here are not outrageous. */
		uint8_t buf[16];
		if (!fread(buf, sizeof(buf), 1, desc->dib.ifp)) {
			return lib_unexpected_eof;
		}

		const uint16_t x = buf_endian16(buf + 4, little_endian);
		const uint16_t y = buf_endian16(buf + 6, little_endian);
		if (desc->type == ico_cursor) {
			desc->images[i].x = x;
			desc->images[i].y = y;
		} else {
			if (x > 1) {
				return lib_invalid_header;
			}
			switch (y) {
			case 0: case 1: case 2: case 4: case 8:
			case 16: case 24: case 32:
				break;
			default:
				return lib_invalid_header;
			}
		}
		desc->images[i].size = buf_endian32(buf + 8, little_endian);
		desc->images[i].offset = buf_endian32(buf + 12, little_endian);
	}
	return lib_ok;
}

enum lib_fail ico_open_file(struct ico_desc *desc, FILE *ifp) {
	/* ICO header:
		Offset  Size    Name
		0       WORD    Reserved   // 0
		2       WORD    Type       // 1 for icons, 2 for cursors
		4       WORD    ImageCount
		6
	*/

	*desc = (struct ico_desc){0};

	uint16_t header[3];
	if (fread(header, sizeof(header), 1, ifp)) {
		const uint16_t count = endian16(header[2], little_endian);
		if (header[0] == 0 && count != 0) {
			uint16_t type = endian16(header[1], little_endian);
			switch (type) {
			case ico_icon:
			case ico_cursor:
				desc->dib.ifp = ifp;
				desc->type = type;
				desc->count = count;
				return lib_ok;
			}
		}
		return lib_invalid_header;
	}
	return lib_unexpected_eof;
}
