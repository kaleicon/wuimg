// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#include "raster/fmt.h"
#include "raster/graphics_adapters.h"
#include "pictor.h"

const char * pictor_palette_str(enum pictor_palette_type type) {
	switch (type) {
	case pictor_no_palette: break;
	case pictor_cga_palette: return "CGA";
	case pictor_pcjr_palette: return "PCJr";
	case pictor_ega_palette: return "EGA";
	case pictor_vga_palette:
	case pictor_vga_too_i_think: return "VGA";
	}
	return "???";
}

const char * pictor_video_mode(const struct pictor_desc *desc) {
	switch (desc->video_mode) {
	case '0': return "40 column text";
	case '1': return "80 column text";
	case '2': return "Monochrome text";
	case '3': return "EGA 43-line text";
	case '4': return "VGA 50-line text";
	case 'A': return "CGA 320x200x4";
	case 'B': return "EGA 320x200x16";
	case 'C': return "CGA 640x200x2";
	case 'D': return "EGA 640x200x16";
	case 'E': return "EGA 640x350x2";
	case 'F': return "EGA 640x350x4";
	case 'G': return "EGA 640x350x16";
	case 'H': return "EGA 720x348x2 (Hercules)";
	case 'I': return "EGA 320x200x16 (Plantronics)";
	case 'J': return "EGA 320x200x16";
	case 'K': return "EGA 640x400x2 (AT&T or Toshiba 3100)";
	case 'L': return "VGA 320x200x256";
	case 'M': return "VGA 640x480x16";
	case 'N': return "EGA 720x348x16 (Hercules InColor)";
	case 'O': return "VGA 640x480x2";
	}
	return "???";
}

static void pictor_interleave(const struct pictor_desc *desc,
struct wuimg *img, const unsigned char *restrict src) {
	bitplane_interleave_plane(img->data, src, img->w, desc->planes, 0,
		img->h);
}

static size_t rle_decode(unsigned char *restrict dst, const size_t dst_len,
const size_t blocks, FILE *ifp) {
	/* Block format:
		Offset  Size    Name
		0       WORD    BlockSize
		2       WORD    DecodedLength   // Ignored by us
		4       BYTE    MarkerByte
		5               Payloads[]

	 * Payload format:
		Offset  Size    Name
		0       BYTE    RunByte

		If MarkerByte == RunByte:
			Offset  Size    Name
			0       BYTE    RunByte
			1       BYTE    LittleCount
			2

			If LittleCount != 0:
				Offset  Size    Name
				0       BYTE    RunByte
				1       BYTE    LittleCount
				2       BYTE    RepeatByte
			Else:
				Offset  Size    Name
				0       BYTE    RunByte
				1       BYTE    _
				2       WORD    BigCount
				3       BYTE    RepeatByte

		Else, RunByte is copied to output
	*/

	unsigned char *block = malloc((1U << 16) - 5);
	if (!block) {
		return 0;
	}

	size_t i = 0;
	for (size_t b = 0; b < blocks && i < dst_len; ++b) {
		if (!fread(block, 5, 1, ifp)) {
			break;
		}
		const uint16_t block_size = buf_endian16l(block);
		const unsigned char run_marker = block[4];
		if (block_size <= 5) {
			break;
		}
		const size_t read = fread(block, 1, block_size - 5, ifp);
		size_t k = 0;
		while (i < dst_len && k < read) {
			const unsigned char run_val = block[k];
			++k;
			if (run_marker == run_val) {
				if (k + 1 >= read) {
					break;
				}

				size_t count;
				if (block[k]) {
					count = block[k];
					++k;
				} else {
					if (k + 3 >= read) {
						break;
					}
					count = buf_endian16l(block + k + 1);
					k += 3;
				}

				if (count > dst_len - i) {
					// DELETE ME: is this ever needed?
					count = dst_len - i;
				}
				memset(dst + i, block[k], count);
				i += count;
				++k;
			} else {
				dst[i] = run_val;
				++i;
			}
		}
	}
	free(block);

	if (i && i < dst_len) {
		// DELETE ME: is this ever needed?
		memset(dst + i, dst[i-1], dst_len - i);
	}
	return i;
}

struct wu_st pictor_decode(const struct pictor_desc *desc, struct wuimg *img) {
	size_t raster_len;
	unsigned char *raster;
	if (desc->interleave) {
		raster_len = strip_base(img->w, 1) * desc->planes * img->h;
		raster = malloc(raster_len);
		if (!raster) {
			return wuerr(wu_alloc_error,
				"failed to allocate interleaved raster");
		}
	} else {
		raster_len = wuimg_size(img);
		raster = img->data;
	}

	size_t written = 0;
	if (desc->blocks) {
		written = rle_decode(raster, raster_len, desc->blocks, desc->ifp);
	} else {
		written = fread(raster, 1, raster_len, desc->ifp);
	}

	if (desc->interleave) {
		pictor_interleave(desc, img, raster);
		free(raster);
	}
	return wuerr_partial(written, raster_len);
}

static struct wu_st load_palette(struct pictor_desc *desc, struct wuimg *img,
const enum pictor_palette_type pal_type, const uint16_t size) {
	desc->pal_type = pal_type;
	const int bpp = desc->depth * desc->planes;
	switch (pal_type) {
	case pictor_no_palette:
		if (bpp == 1 || bpp == 8) {
			return WU_OK;
		} else if (size != 0) {
			return wuerr(wu_invalid_header,
				"no palette but size is != 0");
		}
		// Other bitdepths use default CGA palette
		break;
	case pictor_cga_palette:
		if (size != 2) {
			return wuerr(wu_invalid_header,
				"cga palette with size != 2");
		}
		break;
	case pictor_pcjr_palette:
	case pictor_ega_palette:
		if (size != 16) {
			return wuerr(wu_invalid_header,
				"ega/pcjr palette with size != 16");
		}
		break;
	case pictor_vga_palette:
	case pictor_vga_too_i_think:
		if (size > 256*3) {
			return wuerr(wu_invalid_header,
				"vga palette with size > 256*3");
		}
		break;
	default:
		return wuerr(wu_invalid_header,
			"bad palette type");
	}

	struct palette *pal = wuimg_palette_init(img);
	if (!pal) {
		return WUERR_HERE(wu_alloc_error);
	}

	unsigned char *buf = (unsigned char *)(pal + 1) - size;
	if (desc->pal_type != pictor_no_palette) {
		if (!fread(buf, size, 1, desc->ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
	}

	const unsigned char cga_modes[][4] = {
		{0, 3, 5, 7},
		{0, 2, 4, 6},
		{0, 3, 4, 7},
		{0, 11, 13, 15},
		{0, 10, 12, 14},
		{0, 11, 12, 15},
	};
	img->bitrange = 2;
	switch (pal_type) {
	case pictor_no_palette:
		if (bpp == 2) {
			for (size_t i = 0; i < 4; ++i) {
				pal->color[i] = cga_palette(cga_modes[0][i]);
			}
		} else {
			for (size_t i = 0; i < 16; ++i) {
				pal->color[i] = cga_palette(i);
			}
		}
		break;
	case pictor_cga_palette:
		;const unsigned char mode = buf[0];
		const unsigned char border = buf[1];
		if (mode >= 6 || border >= 16) {
			return wuerr(wu_invalid_header,
				"bad cga palette mode/border");
		}

		pal->color[0] = cga_palette(border);
		for (size_t i = 1; i < 4; ++i) {
			pal->color[i] = cga_palette(cga_modes[mode][i]);
		}
		break;
	case pictor_pcjr_palette:
	case pictor_ega_palette:
		for (size_t i = 0; i < 16; ++i) {
			pal->color[i] = ega_palette(buf[i]);
		}
		break;
	case pictor_vga_palette:
	case pictor_vga_too_i_think:
		img->bitrange = 6;
		palette_from_rgb8_bitrange(pal, buf, size/3, img->bitrange);
		break;
	}
	return WU_OK;
}

struct wu_st pictor_parse(struct pictor_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* Pictor file header (after id):
		Offset  Type    Name
		0       u8      ID[2]
		2       u16     Width
		4       u16     Height
		6       u16     ScreenX       // X of lower left corner of image
		8       u16     ScreenY       // Y of lower left corner of image
		10      u8      PlaneInfo     // [1]
		|       Bits
		|       7-4     BitPlanes     // 1 must be added
		|       3-0     BitDepth
		|
		11      u8      PaletteMarker // 0xff
		12      u8      VideoMode     // Video mode of image
		13      u16     PaletteType   // Type of color palette
		15      u16     PaletteSize   // Size of color palette
		17      u8      Palette[PaletteSize]
		...
		+0      u16     NrOfBlocks // If 0, raster is uncompressed
		+2

	 * [1] Multiple planes are valid only if BitDepth is 1.
	 *     Planes are stored separated per scanline, and must be joined to
	 *     get the palette index.
	*/

	const uint8_t magic[] = {0x34, 0x12};
	uint8_t buf[17];
	if (!fread(buf, sizeof(buf), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(buf, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	} else if (buf[11] != 0xff) {
		return wuerr(wu_invalid_header, "palette marker != 0xff");
	}

	*desc = (struct pictor_desc) {
		.ifp = ifp,
		.depth = buf[10] & 0x0f,
		.planes = (buf[10] >> 4) + 1,
		.x = buf_endian16l(buf + 6),
		.y = buf_endian16l(buf + 8),
		.video_mode = (char)buf[12],
	};
	switch (desc->depth) {
	case 1:
		if (desc->planes > 4) {
			return wuerr(wu_invalid_header,
				"1-bit image with more then 4 planes");
		}
		break;
	case 2: case 4: case 8:
		if (desc->planes != 1) {
			return wuerr(wu_invalid_header,
				"planes != 1 with bitdepth > 1");
		}
		break;
	default:
		return wuerr(wu_invalid_header,
			"depth is not 1, 2, 4, or 8");
	}

	img->w = buf_endian16l(buf + 2);
	img->h = buf_endian16l(buf + 4);
	img->channels = 1;
	img->bitdepth = desc->depth;
	img->mirror = true;

	const struct wu_st status = load_palette(desc, img,
		buf_endian16(buf + 13, little_endian),
		buf_endian16(buf + 15, little_endian));
	if (!wu_isok(status)) {
		return status;
	}

	if (!fread(buf, 2, 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	desc->blocks = buf_endian16(buf, little_endian);
	if (desc->planes != 1) {
		if (img->mode == image_mode_palette) {
			img->bitdepth = 8;
			desc->interleave = true;
		} else {
			img->channels = desc->planes;
			wuimg_plane_init(img);
		}
	}
	return WU_OK;
}
