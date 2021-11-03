#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#include "../common.h"
#include "../raster/raster.h"
#include "dib.h"

struct ico_buf {
	size_t stride, size;
	unsigned char *buf;
};

enum dib_rle_marker {
        dib_end_of_scan_line = 0,
        dib_end_of_rle = 1,
        dib_delta = 2,
};

const uint32_t BITFIELD_SHIFT = 16;

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

	if (desc->depth <= 8) {
		for (size_t i = 0; i < desc->pal_entries; ++i) {
			struct pix_rgba8 *c = desc->r.palette->color + i;
			printf("%zu: %d %d %d %d\n", i, c->r, c->g, c->b, c->a);
		}
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

static enum lib_fail validate_dib_header(struct dib_desc *desc,
const int32_t width, const int32_t height, const uint16_t planes,
const uint16_t depth, const uint32_t compression, const uint32_t size,
const uint32_t colors) {
	if (width < 1) {
		return lib_invalid_header;
	}
	if (!height) {
		return lib_invalid_header;
	}
	desc->r.w = (unsigned)width;
	desc->r.h = (unsigned)(height > 0 ? height : -height);
	desc->order = (height > 0) ? dib_bottom_up : dib_top_down;

	if (planes > 1) {
		return lib_unsupported_format;
	}

	switch (depth) {
	case 1: case 2: case 4: case 8:
		desc->r.ch = 1;
		desc->r.bitdepth = (unsigned char)depth;
		if (colors) {
			if (colors > 256) {
				return lib_invalid_header;
			}
			desc->pal_entries = colors;
		} else {
			desc->pal_entries = 1 << depth;
		}
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
		desc->size = size;
		break;
	case dib_4bit_rle:
		if (depth != 4 || desc->order == dib_top_down || size == 0) {
			return lib_invalid_header;
		}
		desc->size = size;
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
	return lib_ok;
}

static enum lib_fail dib_parse_type3_header(struct dib_desc *desc) {
	/* Type 3 and up DIB header (after header size field).

	 * BITMAPINFOHEADER:
		Offset  Size    Name
		0       LONG    Width           // Width in pixels
		4       LONG    Height          // Height in pixels
		8       WORD    Planes          // Nr of color planes (always 1)
		10      WORD    BitsPerPixel
		12      DWORD   Compression     // Compression method
		16      DWORD   SizeOfBitmap    // Size of RLE bitmap
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
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return lib_unexpected_eof;
	}

	enum lib_fail status = validate_dib_header(desc,
		(int32_t)buf_endian32(buf, little_endian),
		(int32_t)buf_endian32(buf + 4, little_endian),
		buf_endian16(buf + 8, little_endian),
		buf_endian16(buf + 10, little_endian),
		buf_endian32(buf + 12, little_endian),
		buf_endian32(buf + 16, little_endian),
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
			return lib_load_pal(desc->ifp, &desc->r.palette,
				lib_pal_rgbx, desc->pal_entries);
		}
	}
	return lib_ok;
}

static enum lib_fail dib_parse_core_header(struct dib_desc *desc) {
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

	const enum lib_fail status = validate_dib_header(desc,
		(int16_t)endian16(buf[0], little_endian),
		(int16_t)endian16(buf[1], little_endian),
		endian16(buf[2], little_endian),
		endian16(buf[3], little_endian),
		dib_no_compression,
		0, 0);
	if (status != lib_ok) {
		return status;
	}

	if (desc->depth <= 8) {
		return lib_load_pal(desc->ifp, &desc->r.palette, lib_pal_rgb,
			desc->pal_entries);
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
	if (fread(&hsize, 1, sizeof(hsize), desc->ifp) != sizeof(hsize)) {
		return lib_unexpected_eof;
	}

	hsize = endian32(hsize, little_endian);
	desc->type = (enum dib_type)hsize;
	desc->r = (struct raster_desc){
		.alignment = 4,
		.layout = pix_bgra,
	};
	enum lib_fail status;
	switch (hsize) {
	case dib_core_header:
		status = dib_parse_core_header(desc);
		break;
	case dib_info_header:
	case dib_v2_info_header:
	case dib_v3_info_header:
	case dib_v4_header:
	case dib_v5_header:
		status = dib_parse_type3_header(desc);
		break;
	default:
		return lib_unsupported_format;
	}
	if (status != lib_ok) {
		return status;
	}

	raster_normalize(&desc->r);
	const size_t raster_size = scanline_length(desc->r.w, desc->depth, 4)
		* desc->r.h;
	switch (desc->compression) {
	case dib_no_compression:
	case dib_bitfield:
		desc->size = raster_size;
		break;
	case dib_8bit_rle:
	case dib_4bit_rle:
		/* E.g. 0x00 0x03 0xff 0xff 0xff... -> 0xff 0xff 0xff...
		 * This is ignoring the obvious infinite 0x00 0x02 0x00 0x00 */
		;const size_t pathological_rle = raster_size * 5 / 3;
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
	 * File header:
		Offset	Size    Name
		0       DWORD   FileSize     // In bytes
		4       WORD    Reserved1
		6       WORD    Reserved2
		8       DWORD   BitmapOffset // Start offset of bitmap in bytes
		12

	 * DIB header follows afterwards.
	*/

	uint32_t buf[3];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
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
	enum lib_fail fail;
	const unsigned char magic[][2] = {
		{'B', 'M'}, // BMP
		{0, 0}, // DDB
	};
	unsigned char sig[2];
	if (fread(sig, 1, sizeof(sig), ifp) == sizeof(sig)) {
		if (!memcmp(magic[0], sig, sizeof(sig))) {
			desc->ifp = ifp;
			return lib_ok;
		} else if (!memcmp(magic[1], sig, sizeof(sig))) {
			fail = lib_unsupported_format;
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

static uint8_t get_bit(const unsigned char *and, const size_t x) {
	return (and[x/8] >> (7 - (x % 8))) & 1;
}

static void ico_32bit_dec(const struct dib_desc *dib, struct pix_rgba8 *dst,
const unsigned char *and, const size_t and_stride) {
	for (size_t y = 0; y < dib->r.h; ++y) {
		struct pix_rgba8 *d = dst + dib->r.w * y;
		const uint8_t *a = and + and_stride * y;
		for (size_t x = 0; x < dib->r.w; ++x) {
			const uint8_t bit = get_bit(a, x);
			if (bit) {
				d[x].a = 0;
			}
		}
	}
}

static unsigned char * ico_word_dec(const struct dib_desc *dib) {
	const size_t xor_stride = raster_stride(&dib->r);
	unsigned char *xor = fread_alloc(dib->ifp, xor_stride, dib->r.h);
	if (!xor) {
		return NULL;
	}

	const size_t and_stride = scanline_length(dib->r.w, 1, 4);
	unsigned char *and = fread_alloc(dib->ifp, and_stride, dib->r.h);
	if (!and) {
		free(xor);
		return NULL;
	}

	if (dib->depth == 32) {
		ico_32bit_dec(dib, (struct pix_rgba8 *)xor, and, and_stride);
	} else {
		for (size_t y = 0; y < dib->r.h; ++y) {
			uint16_t *d = (uint16_t *)(xor + xor_stride * y);
			const uint8_t *a = and + and_stride * y;
			for (size_t x = 0; x < dib->r.w; ++x) {
				const uint8_t bit = get_bit(a, x);
				int i = (endian16(d[x], little_endian) & 0x7fff)
					| (!bit << 15);
				d[x] = (uint16_t)i;
			}
		}
	}
	free(and);
	return xor;
}

static void ico_buf_sizes(const struct dib_desc *dib, struct ico_buf *buf,
const unsigned char depth) {
	buf->stride = scanline_length(dib->r.w, depth, 4);
	buf->size = buf->stride * dib->r.h;
}

static bool ico_truecolor_expands(const struct dib_desc *dib,
struct ico_buf *restrict dst, struct ico_buf *restrict xor,
struct ico_buf *restrict and) {
	ico_buf_sizes(dib, dst, 32);
	ico_buf_sizes(dib, xor, dib->depth);
	ico_buf_sizes(dib, and, 1);

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
			const uint8_t bit = (a[x/8] >> (7 - (x % 8)));
			d[x*4] = s[x*3];
			d[x*4 + 1] = s[x*3 + 1];
			d[x*4 + 2] = s[x*3 + 2];
			d[x*4 + 3] = (bit ? 0x00 : 0xff);
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

	if (dib->r.w > UCHAR_MAX || dib->r.h > UCHAR_MAX
	|| dib->type != dib_info_header
	|| dib->compression != dib_no_compression) {
		return lib_invalid_header;
	}
	// dib_parse_header() will drop us at the start of the XOR bitmap.
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
		if (fread(buf, 1, sizeof(buf), desc->dib.ifp) != sizeof(buf)) {
			return lib_unexpected_eof;
		}

		uint16_t x = buf_endian16(buf + 4, little_endian);
		uint16_t y = buf_endian16(buf + 6, little_endian);
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

	uint16_t header[3];
	if (fread(header, 1, sizeof(header), ifp) == sizeof(header)) {
		const uint16_t count = endian16(header[2], little_endian);
		if (header[0] == 0 && count != 0) {
			uint16_t type = endian16(header[1], little_endian);
			switch (type) {
			case ico_icon:
			case ico_cursor:
				desc->dib.ifp = ifp;
				desc->dib.r.palette = NULL;
				desc->type = type;
				desc->count = count;
				return lib_ok;
			}
		}
		return lib_invalid_header;
	}
	return lib_unexpected_eof;
}
