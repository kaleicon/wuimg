#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

//#include "../common.h"
#include "../raster/unpack.h"

#include "tim.h"

void tim_cleanup(struct tim_desc *desc) {
	raster_free(&desc->r);
}

static void special_transparency_process(uint16_t *buf, const size_t nmemb) {
	const uint16_t stp_bit = 1 << 15;
	const uint16_t mask = stp_bit - 1;
	for (size_t i = 0; i < nmemb; ++i) {
		uint16_t w = endian16(buf[i], little_endian);
		if (w & mask) {
			w ^= stp_bit;
		}
		buf[i] = w;
	}
}

size_t tim_decode(const struct tim_desc *desc, void *restrict dst) {
	const size_t read = fread(dst, 1, raster_size(&desc->r), desc->ifp);
	if (desc->r.attr == pix_packing_1555) {
		special_transparency_process(dst, read/2);
	}
	return read;
}

static enum lib_fail read_cluts(struct tim_desc *desc,
unsigned char header[static 12]) {
	struct tim_clut *clut = &desc->clut;
	clut->x = buf_endian16(header + 4, little_endian);
	clut->y = buf_endian16(header + 6, little_endian);
	clut->nb = buf_endian16(header + 10, little_endian);
	if (!clut->nb) {
		return lib_invalid_header;
	}

	const size_t colors = 1 << desc->r.bitdepth;
	if (colors != buf_endian16(header + 8, little_endian)) {
		return lib_invalid_header;
	}

	desc->r.palette = malloc(sizeof(*clut->data) * clut->nb);
	if (!desc->r.palette) {
		return lib_alloc_error;
	}

	for (size_t n = 0; n < clut->nb; ++n) {
		struct raster_pal *pal = desc->r.palette + n;
		uint16_t *buf = (uint16_t *)(pal + 1) - colors;
		if (fread(buf, sizeof(*buf), colors, desc->ifp) != colors) {
			return lib_unexpected_eof;
		}

		special_transparency_process(buf, colors);
		unpack_strip(pal, buf, colors, 16, pix_packing_1555,
			op_expand);
	}
	return lib_ok;
}

enum lib_fail tim_parse_header(struct tim_desc *desc) {
	/* TIM header (little-endian) (after id):
		Offset  Size    Name
		0       DWORD   Flags:
		|
		|       Bits    Name
		|       0-2     Bitmap type:
		|               000: 4bpp
		|               001: 8bpp
		|               010: 16bpp // A1_B5G5R5[1], MSB to LSB
		|               011: 24bpp // R8G8B8
		|               100: Mixed
		|       3       CLUT (a.k.a. palette):
		|               0: No CLUT
		|               1: Has CLUT
		|       4-      Reserved
		4

	 * CLUT header, if present:
		Offset  Size    Name
		0       DWORD   SizeOfCLUT // Including this header
		4       WORD    PaletteOrigX
		6       WORD    PaletteOrigY
		8       WORD    NbOfColors // Always 2^bpp
		10      WORD    NbOfCLUTs
		12      VAR     CLUTData   // 16bit A1_R5G5B5[1]
		??

	 * Image header:
		Offset  Size    Name
		0       DWORD   SizeOfImage // Including this header
		4       WORD    ImageOrigX
		6       WORD    ImageOrigY
		8       WORD    ImageWidth  // WORDs per line
		10      WORD    ImageHeight
		12      VAR     ImageData

	 * [1] For 16bit image data, the "A" bit (called the Special
	 *     Transparency Proccesing bit) is not really Alpha:
	 *     If transparency processing is enabled in the PSX, then if
	 *     the bit is set the color is transparent, except if the color
	 *     is pure black (0,0,0), where it's opaque if set.
	 *     In programming terms: for non-black colors, the STP bit must
	 *     be flipped to convert to Alpha.
	 */

	unsigned char header[16];
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	const uint32_t flags = buf_endian32(header, little_endian);
	uint8_t depth;
	switch (flags & 0x7) {
	case 0: depth = 4; break;
	case 1: depth = 8; break;
	case 2: depth = 16; break;
	case 3: depth = 24; break;
	case 4: return lib_tim_mixed_bitdepth;
	default: return lib_invalid_header;
	}

	if (depth == 24) {
		desc->r.ch = 3;
		desc->r.bitdepth = 8;
	} else {
		desc->r.ch = 1;
		desc->r.bitdepth = depth;
		if (depth == 16) {
			desc->r.attr = pix_packing_1555;
		}
	}

	if (flags & 0x8) {
		if (depth > 8) {
			return lib_invalid_header;
		}

		const enum lib_fail status = read_cluts(desc, header + 4);
		if (status != lib_ok) {
			return status;
		}
		const size_t image_header = sizeof(header) - 4;
		if (!fread(header + 4, image_header, 1, desc->ifp)) {
			return lib_unexpected_eof;
		}
	}

	desc->x = buf_endian16(header + 8, little_endian);
	desc->y = buf_endian16(header + 10, little_endian);

	const size_t line_len = buf_endian16(header + 12, little_endian);
	desc->r.w = line_len * 16 / depth;
	desc->r.h = buf_endian16(header + 14, little_endian);
	desc->r.alignment = 2;
	raster_normalize(&desc->r);
	return lib_ok;
}

enum lib_fail tim_open_file(struct tim_desc *desc, FILE *ifp) {
	const unsigned char sig[] = {0x10, 0, 0, 0};
	const enum lib_fail st = lib_sigcmp(sig, sizeof(sig), ifp);
	if (st == lib_ok) {
		memset(desc, 0, sizeof(*desc));
		desc->ifp = ifp;
	}
	return st;
}
