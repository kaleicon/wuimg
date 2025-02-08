// SPDX-License-Identifier: 0BSD
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
		const uint16_t block_size = buf_endian16(block, little_endian);
		const unsigned char run_marker = block[4];
		if (block_size <= 5) {
			break;
		}
		const size_t read = fread(block, 1, block_size - 5, ifp);
		if (!read) {
			break;
		}

		size_t k = 0;
		do {
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
					count = buf_endian16(block + k + 1,
						little_endian);
					k += 3;
				}

				if (count > dst_len - i) {
					count = dst_len - i;
				}
				memset(dst + i, block[k], count);
				i += count;
				++k;
			} else {
				dst[i] = run_val;
				++i;
			}
		} while (k < read && i < dst_len);
	}
	free(block);

	if (i && i < dst_len) {
		memset(dst + i, dst[i-1], dst_len - i);
	}
	return i;
}

size_t pictor_decode(const struct pictor_desc *desc, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}

	size_t raster_len;
	unsigned char *raster;
	if (desc->interleave) {
		raster_len = strip_base(img->w, 1) * desc->planes * img->h;
		raster = malloc(raster_len);
		if (!raster) {
			return 0;
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
	return written;
}

static enum wu_error load_palette(struct pictor_desc *desc, struct wuimg *img,
const enum pictor_palette_type pal_type, const uint16_t size) {
	desc->pal_type = pal_type;
	const int bpp = desc->depth * desc->planes;
	switch (pal_type) {
	case pictor_no_palette:
		if (bpp == 1 || bpp == 8) {
			return wu_ok;
		} else if (size != 0) {
			return wu_invalid_header;
		}
		break;
	case pictor_cga_palette:
		if (size != 2) {
			return wu_invalid_header;
		}
		break;
	case pictor_pcjr_palette:
	case pictor_ega_palette:
		if (size != 16) {
			return wu_invalid_header;
		}
		break;
	case pictor_vga_palette:
	case pictor_vga_too_i_think:
		if (size > 768) {
			return wu_invalid_header;
		}
		break;
	default:
		return wu_invalid_header;
	}

	struct palette *pal = wuimg_palette_init(img);
	if (!pal) {
		return wu_alloc_error;
	}

	unsigned char *buf = (unsigned char *)(pal + 1) - size;
	if (desc->pal_type != pictor_no_palette) {
		if (!fread(buf, size, 1, desc->ifp)) {
			return wu_unexpected_eof;
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
			return wu_invalid_header;
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
		;const unsigned maxval = (1 << 6) - 1;
		const unsigned scale = (UCHAR_MAX << 8) / maxval + 1;
		for (int i = 0; i < size / 3; ++i) {
			pal->color[i].r = (unsigned char)((buf[i*3] * scale) >> 8);
			pal->color[i].g = (unsigned char)((buf[i*3+1] * scale) >> 8);
			pal->color[i].b = (unsigned char)((buf[i*3+2] * scale) >> 8);
			pal->color[i].a = 0xff;
		}
		break;
	}
	return wu_ok;
}

enum wu_error pictor_read_header(struct pictor_desc *desc, struct wuimg *img) {
	/* Pictor file header (after id):
		Offset  Type    Name
		0       u16     Width
		2       u16     Height
		4       u16     ScreenX       // X of lower left corner of image
		6       u16     ScreenY       // Y of lower left corner of image
		8       u8      PlaneInfo     // [1]
		|       Bits
		|       7-4     BitPlanes     // 1 must be added
		|       3-0     BitDepth
		|
		9       u8      PaletteMarker // 0xff
		10      u8      VideoMode     // Video mode of image
		11      u16     PaletteType   // Type of color palette
		13      u16     PaletteSize   // Size of color palette
		15      u8      Palette[PaletteSize]
		...
		+0      u16     NrOfBlocks // If 0, raster is uncompressed
		+2

	 * [1] Multiple planes are valid only if BitDepth is 1.
	 *     Planes are stored separated per scanline, and must be joined to
	 *     get the palette index.
	*/

	uint8_t buf[15];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	desc->depth = buf[8] & 0x0f;
	desc->planes = (buf[8] >> 4) + 1;
	switch (desc->depth) {
	case 1:
		if (desc->planes > 4) {
			return wu_invalid_header;
		}
		break;
	case 2: case 4: case 8:
		if (desc->planes != 1) {
			return wu_invalid_header;
		}
		break;
	default:
		return wu_invalid_header;
	}

	img->w = buf_endian16(buf, little_endian);
	img->h = buf_endian16(buf + 2, little_endian);
	img->channels = 1;
	img->bitdepth = desc->depth;
	img->mirror = true;

	desc->x = buf_endian16(buf + 4, little_endian);
	desc->y = buf_endian16(buf + 6, little_endian);

	if (buf[9] != 0xff) {
		return wu_invalid_header;
	}
	desc->video_mode = (char)buf[10];
	const enum wu_error status = load_palette(desc, img,
		buf_endian16(buf + 11, little_endian),
		buf_endian16(buf + 13, little_endian));
	if (status != wu_ok) {
		return status;
	}

	if (!fread(buf, 2, 1, desc->ifp)) {
		return wu_unexpected_eof;
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
	return wuimg_verify(img);
}

enum wu_error pictor_open_file(struct pictor_desc *desc, FILE *ifp) {
	const uint8_t magic[] = {0x34, 0x12};
	const enum wu_error st = fmt_sigcmp(magic, sizeof(magic), ifp);
	if (st == wu_ok) {
		*desc = (struct pictor_desc) {
			.ifp = ifp,
		};
	}
	return st;
}
