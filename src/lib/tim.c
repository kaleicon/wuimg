// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "raster/fmt.h"
#include "tim.h"

void tim_cleanup(struct tim_desc *desc) {
	if (desc->clut.clut) {
		for (size_t i = 0; i < desc->clut.nb; ++i) {
			palette_unref(desc->clut.clut[i]);
		}
		free(desc->clut.clut);
	}
	free(desc->raster);
}

static void special_transparency_process(uint16_t *buf, const size_t nmemb) {
	const uint16_t stp_bit = 1 << 15;
	const uint16_t mask = stp_bit - 1;
	for (size_t i = 0; i < nmemb; ++i) {
		uint16_t w = endian16l(buf[i]);
		buf[i] = w ^ ((w & mask) ? stp_bit : 0);
	}
}

static void stp_callback(void *restrict data, const size_t len,
void *restrict _u) {
	(void)_u;
	special_transparency_process(data, len/2);
}

void tim_alt_clut(struct tim_desc *desc, const struct wuimg *main,
struct wuimg *alt, const uint16_t clut_nb) {
	*alt = *main;
	alt->u.palette = palette_ref(desc->clut.clut[clut_nb]);
}

struct wu_st tim_decode_main(struct tim_desc *desc, struct wuimg *img) {
	const size_t size = wuimg_size(img);
	desc->raster = malloc(size);
	if (!desc->raster) {
		return WUERR_HERE(wu_alloc_error);
	}
	img->data = desc->raster;
	img->borrowed = true;
	return wuerr_partial((img->mode == image_mode_bitfield
		? fmt_load_raster_callback(img, desc->ifp, stp_callback, NULL)
		: fmt_load_raster(img, desc->ifp)),
		size);
}

static struct wu_st read_cluts(struct tim_desc *desc, struct wuimg *img,
unsigned char header[static 12]) {
	struct tim_clut *clut = &desc->clut;
	clut->x = buf_endian16l(header + 4);
	clut->y = buf_endian16l(header + 6);
	clut->nb = buf_endian16l(header + 10);
	if (!clut->nb) {
		return wuerr(wu_invalid_header, "palette count is 0");
	}

	const size_t colors = buf_endian16l(header + 8);
	if (colors > 0x100) {
		return wuerr(wu_invalid_header, "palette entries > 256");
	}

	clut->clut = small_calloc(clut->nb, sizeof(*clut->clut));
	if (!clut->clut) {
		return WUERR_HERE(wu_alloc_error);
	}
	for (size_t n = 0; n < clut->nb; ++n) {
		struct palette *pal = palette_new();
		if (!pal) {
			return WUERR_HERE(wu_alloc_error);
		}
		clut->clut[n] = pal;
		uint16_t *buf = (uint16_t *)(pal + 1) - colors;
		if (fread(buf, sizeof(*buf), colors, desc->ifp) != colors) {
			return WUERR_HERE(wu_unexpected_eof);
		}

		special_transparency_process(buf, colors);
		struct bitfield bf;
		bitfield_from_id(&bf, 0x1555, 16);
		bitfield_unpack(&bf, pal->color, buf, colors);
	}
	wuimg_palette_set(img, palette_ref(clut->clut[0]));
	return WU_OK;
}

struct wu_st tim_parse(struct tim_desc *desc, struct wuimg *img, FILE *ifp) {
	/* TIM header:
		Offset  Size    Name
		0       BYTE    ID[4]
		4       DWORD   Flags:
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

	unsigned char header[20];
	const unsigned char sig[] = {0x10, 0, 0, 0};
	*desc = (struct tim_desc) {.ifp = ifp};
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(header, sig, sizeof(sig))) {
		return wuerr(wu_invalid_header, "ID != 0x10");
	}

	const uint32_t flags = buf_endian32l(header + 4);
	uint8_t depth;
	switch (flags & 0x7) {
	case 0: depth = 4; break;
	case 1: depth = 8; break;
	case 2: depth = 16; break;
	case 3: depth = 24; break;
	case 4: return wuerr(wu_samples_wanted, "mixed bitdepth");
	default: return wuerr(wu_invalid_header, "unknown bitdepth");
	}

	if (depth == 24) {
		img->channels = 3;
		img->bitdepth = 8;
	} else {
		img->channels = 1;
		img->bitdepth = depth;
		img->bit = depth == 4 ? little_endian : big_endian;
		if (depth == 16 && !wuimg_bitfield_from_id(img, 0x1555)) {
			return WUERR_HERE(wu_alloc_error);
		}
	}

	if (flags & 0x8) {
		if (depth > 8) {
			return wuerr(wu_invalid_header,
				"paletted image with depth > 8");
		}

		const struct wu_st status = read_cluts(desc, img, header + 8);
		if (!wu_isok(status)) {
			return status;
		}
		const size_t off = 8;
		const size_t image_header = sizeof(header) - off;
		if (!fread(header + off, image_header, 1, desc->ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
	}

	desc->x = buf_endian16l(header + 12);
	desc->y = buf_endian16l(header + 14);

	const size_t line_len = buf_endian16l(header + 16);
	img->w = line_len * 16 / depth;
	img->h = buf_endian16l(header + 18);
	img->align_sh = 1;
	return wuimg_verify_st(img);
}
