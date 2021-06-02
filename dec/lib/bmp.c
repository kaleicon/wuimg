#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../../common.h"
#include "common/unpack.h"
#include "common/composite.h"
#include "bmp.h"

typedef uint32_t ubits_t;
const ubits_t BITFIELD_SHIFT = 16;

void bmp_cleanup(const struct bmp_desc *desc) {
	if (desc->compression != bmp_bitfield) {
		free(desc->bmp.pal);
	}
}

unsigned int bmp_get_row_alignment(const struct bmp_desc *desc) {
	switch (desc->bitdepth) {
	case 1:
		return 1;
	case 4:
		if (desc->compression != bmp_4bit_rle) {
			return 1;
		}
		// fallthrough
	case 2:
	case 8:
		if (desc->bmp.pal) {
			return 1;
		}
	}
	return 4;
}

bool bmp_has_alpha(const struct bmp_desc *desc) {
	if (desc->compression == bmp_bitfield) {
		return desc->bmp.bf.p[3].mask;
	}
	return desc->bitdepth == 32;
}

static void check_decode(const unsigned char *restrict end,
const unsigned char *restrict expected_end) {
	if ( ((uintptr_t)(end + 3) & ~0x3U) < (uintptr_t)expected_end) {
		puts(RASTER_EOF);
	}
}

static unsigned char * uncompressed_expand(const struct bmp_desc *desc,
unsigned char *restrict raster) {
	const unsigned char ch = 3;
	size_t dims = desc->w * desc->h;
	if (desc->bmp.pal) {
		dims *= ch;
	}

	unsigned char *output = malloc(dims);
	if (!output) {
		free(raster);
		return NULL;
	}

	if (desc->bmp.pal) {
		strip_colormap(output, raster, desc->bmp.pal, desc->w, desc->h,
			4, ch, desc->bitdepth);
	} else {
		strip_unpack(output, raster, desc->w, desc->h, 4,
			op_unpack, desc->bitdepth);
	}

	free(raster);
	return output;
}

static unsigned char * uncompressed_decode(const struct bmp_desc *desc,
unsigned char *restrict raster) {
	switch (desc->bitdepth) {
	case 8:
		if (!desc->bmp.pal) {
			return raster;
		}
		// fallthrough
	case 4:
	case 2:
	case 1:
		if (desc->expand) {
			return uncompressed_expand(desc, raster);
		}
		// fallthrough
	case 16:
	case 24:
	case 32:
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
			case bmp_end_of_scan_line:
				raster += (size_t)(raster_limit - raster) % scan_len;
				rle += 2;
				break;
			case bmp_end_of_rle:
				return raster;
			case bmp_delta:
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
unsigned char *restrict rle) {
	// Note: We output in 8-bits.
	const size_t row = scanline_length(desc->w, 8, 4);
	const size_t raster_len = row * desc->h;
	unsigned char *raster = malloc(raster_len);
	if (!raster) {
		free(rle);
		return NULL;
	}

	const size_t rle_len = desc->data_len - desc->data_len % 2;
	unsigned char *raster_end = rle_loop4(raster,
		raster + raster_len, rle, rle + rle_len, row);
	free(rle);

	check_decode(raster_end, raster + raster_len);

	if (desc->bmp.pal) {
		const size_t out_len = desc->w * desc->h * 3;
		unsigned char *output = malloc(out_len);
		if (output) {
			strip_colormap(output, raster, desc->bmp.pal, desc->w,
				desc->h, 8, 3, 8);
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
			case bmp_end_of_scan_line:
				raster += (size_t)(raster_limit - raster)
					% scan_len;
				rle += 2;
				break;
			case bmp_end_of_rle:
				return raster;
			case bmp_delta:
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
unsigned char *restrict rle) {
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

static ubits_t expand_bits(const ubits_t word, const ubits_t mask,
const ubits_t shift, const ubits_t scale) {
	return (((word >> shift) & mask) * scale) >> BITFIELD_SHIFT;
}

static unsigned char * bitfield_decode(const struct bmp_desc *desc, void *data) {
	const struct bmp_bitfield *bf = &desc->bmp.bf;

	const size_t bytes = bf->high_depth + 1;
	const size_t ch = bf->p[3].mask ? 4 : 3;
	const size_t pixels = desc->w * desc->h;
	void *out = malloc(pixels * ch * bytes);
	if (!out) {
		free(data);
		return NULL;
	}

	for (size_t i = 0; i < pixels; ++i) {
		ubits_t word;
		if (desc->bitdepth == 32) {
			word = endian32(((uint32_t *)data)[i], little_endian);
		} else {
			word = endian16(((uint16_t *)data)[i], little_endian);
		}

		const struct bmp_bitparams *p = bf->p;
		for (size_t k = 0; k < ch; ++k) {
			const ubits_t val = expand_bits(word, p[k].mask,
				p[k].shift, p[k].scale);
			if (bytes == 2) {
				((uint16_t *)out)[i*ch + k] = (uint16_t)val;
			} else {
				((uint8_t *)out)[i*ch + k] = (uint8_t)val;
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
		return uncompressed_decode(desc, data);
	case bmp_8bit_rle:
		return rle8_decode(desc, data);
	case bmp_4bit_rle:
		return rle4_decode(desc, data);
	case bmp_bitfield:
		return bitfield_decode(desc, data);
	}
	free(data);
	return NULL;
}

struct colormap * bmp_take_colormap(struct bmp_desc *desc) {
	if (desc->compression != bmp_bitfield) {
		struct colormap *m = desc->bmp.pal;
		desc->bmp.pal = NULL;
		return m;
	}
	return NULL;
}

static bool validate_file_size(struct bmp_desc *desc) {
	const size_t file_size = (size_t)file_get_remaining(desc->ifp);

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
	return true;
}

static enum lib_fail load_mask(struct bmp_desc *desc) {
	const size_t len = desc->type < bmp_v3_info_header ? 3 : 4;
	uint32_t buf[4];
	if (fread(buf, sizeof(*buf), len, desc->ifp) != len) {
		return lib_unexpected_eof;
	}

	loop_endian32(buf, little_endian, len);
	// Switch to BGRA order, for consistency
	const uint32_t t = buf[0];
	buf[0] = buf[2];
	buf[2] = t;

	struct bmp_bitfield *bitfield = &desc->bmp.bf;
	unsigned maxdepth = 0;
	for (size_t i = 0; i < len; ++i) {
		const size_t bits = sizeof(buf[i]) * 8;

		unsigned zeros = 0;
		while (zeros < bits && !(buf[i] & (1 << zeros)) ) {
			++zeros;
		}
		bitfield->p[i].shift = zeros;

		unsigned ones = zeros;
		while (ones < bits && (buf[i] & (1 << ones)) ) {
			++ones;
		}
		maxdepth = umax(ones - zeros, maxdepth);

		bitfield->p[i].mask = buf[i] >> zeros;
		if (ones < bits && buf[i] >> ones) {
			return lib_invalid_header;
		}
	}
	if (maxdepth > desc->bitdepth / 2) {
		return lib_invalid_header;
	}

	bitfield->high_depth = maxdepth > 8;
	const unsigned target = maxdepth > 8 ? 0xffff : 0xff;
	for (size_t i = 0; i < len; ++i) {
		bitfield->p[i].scale = (target << BITFIELD_SHIFT)
			/ bitfield->p[i].mask + 1;
	}

	if (len == 3) {
		memset(bitfield->p + 3, 0, sizeof(*bitfield->p));
	}
	return lib_ok;
}

static enum lib_fail load_palette(struct bmp_desc *desc) {
	struct colormap *pal = malloc(sizeof(*pal) * 256);
	if (!pal) {
		return lib_alloc_error;
	}

	/* Palettes may be smaller than 1U << bitdepth, but the calculation is
	 * a bother plus we know where the bitmap is, so we'll just pretend
	 * that doesn't happen and fill the palette with whatever garbage we get. */
	const size_t entries = 1U << desc->bitdepth;
	// BMP type 2 entries are BGR, while type >= 3 are BGR0
	const size_t entry_len = desc->type == bmp_type2 ? 3 : 4;

	const size_t offset = (desc->type == bmp_type2) * entries;
	unsigned char *bytes = (unsigned char *)pal + offset;
	fread(bytes, entry_len, entries, desc->ifp);
	if (entry_len == 3) {
		for (size_t i = 0; i < entries; ++i) {
			pal[i].r = bytes[i*3];
			pal[i].g = bytes[i*3+1];
			pal[i].b = bytes[i*3+2];
			pal[i].a = 0xff;
		}
	} else {
		for (size_t i = 0; i < entries; ++i) {
			pal[i].a = 0xff;
		}
	}

	desc->bmp.pal = pal;
	return lib_ok;
}

static enum lib_fail validate_bitmap_header(struct bmp_desc *desc,
const int32_t width, const int32_t height, const uint16_t planes,
const uint16_t depth, const uint32_t compression) {
	if (width > 0) {
		desc->w = (uint32_t)width;
	} else {
		return lib_invalid_header;
	}

	if (height > 0) {
		desc->h = (uint32_t)height;
		desc->order = bmp_bottom_up;
	} else if (height < 0) {
		desc->h = (uint32_t)(-height);
		desc->order = bmp_top_down;
	} else {
		return lib_invalid_header;
	}

	if (planes != 1) {
		return lib_unsupported_format;
	}

	switch (depth) {
	case 1: case 2: case 4: case 8: case 16: case 24: case 32:
		break;
	default:
		return lib_invalid_header;
	}

	switch (compression) {
	case bmp_no_compression:
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
		break;
	case bmp_bitfield:
		if (depth != 16 && depth != 32) {
			return lib_invalid_header;
		}
		break;
	default:
		return lib_invalid_header;
	}

	desc->bitdepth = (unsigned char)depth;
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

	 * The following fields are part of BITMAPV2INFOHEADER, but they may
	 * still follow the previous one instead of a palette if the
	 * Compression field is 3.
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

	desc->scan_len = scanline_length(desc->w, desc->bitdepth, 4);
	if (desc->compression == bmp_bitfield) {
		fseek(desc->ifp, 20, SEEK_CUR);
		return load_mask(desc);
	} else if (desc->bitdepth <= 8) {
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

	desc->scan_len = scanline_length(desc->w, desc->bitdepth, 4);
	if (desc->bitdepth <= 8) {
		return load_palette(desc);
	} else if (desc->bitdepth != 24) {
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
	const uint32_t header_size = endian32(buf[3], little_endian);

	enum lib_fail status;
	switch (header_size) {
	case bmp_type2:
		desc->type = (enum bmp_type)header_size;
		status = bmp_type2_parse_header(desc);
		break;
	case bmp_info_header:
	case bmp_v2_info_header:
	case bmp_v3_info_header:
	case bmp_v4_header:
	case bmp_v5_header:
		desc->type = (enum bmp_type)header_size;
		status = bmp_type3_parse_header(desc);
		break;
	default:
		return lib_unsupported_format;
	}

	if (status == lib_ok) {
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
			desc->bmp.pal = NULL;
			desc->expand = false;
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
