// SPDX-License-Identifier: 0BSD
#include <stdbool.h>

#include "misc/common.h"
#include "raster/fmt.h"
#include "tim.h"

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

static void stp_callback(void *restrict data, const size_t len,
void *restrict user) {
	(void)user;
	special_transparency_process(data, len/2);
}

size_t tim_decode(const struct tim_desc *desc, struct wuimg *img) {
	size_t read = 0;
	if (wuimg_alloc_noverify(img)) {
		if (img->mode == image_mode_bitfield) {
			read = fmt_load_raster_callback(img, desc->ifp,
				stp_callback, NULL);
		} else {
			read = fmt_load_raster(img, desc->ifp);
		}
	}
	return read;
}

static enum wu_error read_cluts(struct tim_desc *desc, struct wuimg *img,
unsigned char header[static 12]) {
	struct tim_clut *clut = &desc->clut;
	clut->x = buf_endian16(header + 4, little_endian);
	clut->y = buf_endian16(header + 6, little_endian);
	clut->nb = buf_endian16(header + 10, little_endian);
	if (!clut->nb) {
		return wu_invalid_header;
	}

	const size_t colors = 1 << img->bitdepth;
	if (colors != buf_endian16(header + 8, little_endian)) {
		return wu_invalid_header;
	}

	struct palette *palette = small_malloc(clut->nb, sizeof(*palette));
	if (!palette) {
		return wu_alloc_error;
	}
	wuimg_palette_set(img, palette);

	for (size_t n = 0; n < clut->nb; ++n) {
		struct palette *pal = palette + n;
		pal->refs = 0;
		uint16_t *buf = (uint16_t *)(pal + 1) - colors;
		if (fread(buf, sizeof(*buf), colors, desc->ifp) != colors) {
			return wu_unexpected_eof;
		}

		special_transparency_process(buf, colors);
		struct bitfield bf;
		bitfield_from_id(&bf, 0x1555, 16);
		bitfield_unpack(&bf, pal->color, buf, colors);
	}
	return wu_ok;
}

enum wu_error tim_parse_header(struct tim_desc *desc, struct wuimg *img) {
	/* TIM header (little-endian) (after id):
		Offset  Size    Name
		0       DWORD   Flags:
		|
		|       Bits    Name
		|       0-2     Bitmap type:
		|               000: 4bpp  // Least significant nibble first
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
		0       DWORD   SizeOfCLUT   // Including this header
		4       WORD    PaletteOrigX
		6       WORD    PaletteOrigY
		8       WORD    NbOfColors   // Always 2^bpp
		10      WORD    NbOfCLUTs
		12      VAR     CLUTData     // 16bit A1_R5G5B5[1]
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
	 *     Transparency Proccesing bit) is not really Alpha.
	 *     If the bit is set the color is transparent, unless the color
	 *     is pure black (0,0,0), then if set it means it's opaque.
	 */

	unsigned char header[16];
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	const uint32_t flags = buf_endian32(header, little_endian);
	uint8_t depth;
	switch (flags & 0x7) {
	case 0: depth = 4; break;
	case 1: depth = 8; break;
	case 2: depth = 16; break;
	case 3: depth = 24; break;
	case 4: return wu_samples_wanted;
	default: return wu_invalid_header;
	}

	if (depth == 24) {
		img->channels = 3;
		img->bitdepth = 8;
	} else {
		img->channels = 1;
		img->bitdepth = depth;
		if (depth == 16 && !wuimg_bitfield_from_id(img, 0x1555)) {
			return wu_alloc_error;
		}
	}

	desc->clut.nb = 0;
	if (flags & 0x8) {
		if (depth > 8) {
			return wu_invalid_header;
		}

		const enum wu_error status = read_cluts(desc, img, header + 4);
		if (status != wu_ok) {
			return status;
		}
		const size_t image_header = sizeof(header) - 4;
		if (!fread(header + 4, image_header, 1, desc->ifp)) {
			return wu_unexpected_eof;
		}
	}

	desc->x = buf_endian16(header + 8, little_endian);
	desc->y = buf_endian16(header + 10, little_endian);

	const size_t line_len = buf_endian16(header + 12, little_endian);
	img->w = line_len * 16 / depth;
	img->h = buf_endian16(header + 14, little_endian);
	img->align_sh = 1;
	return wuimg_verify(img);
}

enum wu_error tim_open_file(struct tim_desc *desc, FILE *ifp) {
	desc->ifp = ifp;
	const unsigned char sig[] = {0x10, 0, 0, 0};
	return fmt_sigcmp(sig, sizeof(sig), ifp);
}
