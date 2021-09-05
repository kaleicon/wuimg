#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#include "../raster/lib.h"
#include "../raster/unpack.h"
#include "../raster/graphics_adapters.h"
#include "../common.h"

#include "pictor.h"

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
	return NULL;
}

void pictor_cleanup(struct pictor_desc *desc) {
	raster_free(&desc->r);
}

static unsigned char * pictor_interleave(unsigned char *restrict src,
struct raster_desc *desc) {
	size_t dims = desc->w * desc->h;
	if (!desc->palette) {
		dims *= desc->ch;
	}
	unsigned char *dst = malloc(dims);
	if (!dst) {
		free(src);
		return NULL;
	}

	vga_interleave(dst, src, desc, desc->h,
		scanline_length(desc->w, desc->bitdepth, 1));
	free(src);

	if (desc->palette) {
		desc->ch = 1;
		desc->bitdepth = 8;
	}
	desc->alignment = 1;
	return dst;
}

static size_t rle_decode(FILE *ifp, unsigned char *restrict out,
const size_t outlen) {
	size_t i = 0;

	uint16_t buf;
	if (!fread(&buf, sizeof(buf), 1, ifp)) {
		return i;
	}

	const size_t min_size = 6; /* 5-byte block header plus min payload. */
	size_t block_size = endian16(buf, little_endian);
	if (block_size < min_size) {
		return i;
	}

	unsigned char *block = malloc(1U << 16);
	if (!block) {
		return i;
	}

	do {
		/* We read block_size bytes at a time, but the block size
		 * includes its own size and it was already read, so we get the
		 * next block size at the end of the buffer. */
		const size_t read = fread(block, 1, block_size, ifp);
		bool last_block;
		size_t rem;
		if (read < min_size) {
			break;
		} else if (read == block_size) {
			rem = read - 2;
			last_block = false;
		} else {
			rem = read;
			last_block = true;
		}

		/* Block format:
			Offset  Size    Name
			-2      WORD    BlockSize       // Already read
			0       WORD    DecodedLength   // Untrusted by us
			2       BYTE    MarkerByte
			3               Payloads[]

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

		const unsigned char run_marker = block[2];
		size_t k = 3;
		do {
			const unsigned char run_val = block[k];
			if (run_marker == run_val) {
				++k;
				if (k + 1 >= rem) {
					break;
				}

				size_t count;
				if (block[k]) {
					count = block[k];
					++k;
				} else {
					if (k + 3 >= rem) {
						break;
					}
					count = buf_endian16(block + k + 1,
						little_endian);
					k += 3;
				}

				count = zumin(count, outlen - i);
				memset(out + i, block[k], count);
				i += count;
			} else {
				out[i] = run_val;
				++i;
			}
			++k;
		} while (k < rem && i < outlen);

		if (i >= outlen || last_block) {
			break;
		}
		block_size = buf_endian16(block + k, little_endian);
	} while (block_size >= min_size);
	free(block);

	return i;
}

unsigned char * pictor_decode(struct pictor_desc *desc) {
	const size_t raster_len = scanline_length(desc->r.w, desc->r.bitdepth, 1)
		* desc->r.ch * desc->r.h;
	unsigned char *raster = malloc(raster_len);
	if (!raster) {
		return NULL;
	}

	size_t written;
	if (desc->blocks) {
		written = rle_decode(desc->ifp, raster, raster_len);
	} else {
		written = fread(raster, 1, raster_len, desc->ifp);
	}
	if (!written) {
		free(raster);
		return NULL;
	} else if (written < raster_len) {
		puts(RASTER_EOF);
	}

	if (desc->r.ch > 1) {
		raster = pictor_interleave(raster, &desc->r);
	}
	return raster;
}

static enum lib_fail load_palette(struct pictor_desc *desc,
const uint16_t size) {
	const int bpp = desc->r.ch * desc->r.bitdepth;

	switch (desc->pal_type) {
	case pictor_no_palette:
		if (bpp == 1 || bpp == 8) {
			return lib_ok;
		}
		break;
	case pictor_cga_palette:
		if (size != 2) {
			return lib_invalid_header;
		}
		break;
	case pictor_pcjr_palette:
	case pictor_ega_palette:
		if (size != 16) {
			return lib_invalid_header;
		}
		break;
	case pictor_vga_palette:
	case pictor_vga_too_i_think:
		if (size > 768) {
			return lib_invalid_header;
		}
		break;
	default:
		return lib_invalid_header;
	}

	struct raster_pal *pal = malloc(sizeof(*pal));
	if (!pal) {
		return lib_alloc_error;
	}
	desc->r.palette = pal;

	unsigned char *buf = (unsigned char *)(pal + 1) - size;
	if (fread(buf, 1, size, desc->ifp) != size) {
		return lib_unexpected_eof;
	}

	const unsigned char cga_modes[][4] = {
		{0, 3, 5, 7},
		{0, 2, 4, 6},
		{0, 3, 4, 7},
		{0, 11, 13, 15},
		{0, 10, 12, 14},
		{0, 11, 12, 15},
	};
	switch (desc->pal_type) {
	case pictor_no_palette:
		if (bpp == 4) {
			for (size_t i = 0; i < 16; ++i) {
				pal->color[i] = cga_palette(i);
			}
		} else {
			for (size_t i = 0; i < 4; ++i) {
				pal->color[i] = cga_palette(cga_modes[0][i]);
			}
		}
		break;
	case pictor_cga_palette:
		if (buf[0] >= 6 || buf[1] >= 16) {
			return lib_invalid_header;
		}
		const unsigned char mode = buf[0];

		pal->color[0] = cga_palette(buf[1]);
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
		; const unsigned maxval = (1 << 6) - 1;
		const unsigned scale = (UCHAR_MAX << 8) / maxval + 1;
		for (int i = 0; i < size / 3; ++i) {
			pal->color[i].r = (unsigned char)((buf[i*3] * scale) >> 8);
			pal->color[i].g = (unsigned char)((buf[i*3+1] * scale) >> 8);
			pal->color[i].b = (unsigned char)((buf[i*3+2] * scale) >> 8);
			pal->color[i].a = 0xff;
		}
		break;
	}
	return lib_ok;
}

enum lib_fail pictor_read_header(struct pictor_desc *desc) {
	/* Pictor file header (after id):
		Offset  Size    Name
		0       WORD    Width;
		2       WORD    Height;
		4       WORD    XOffset;     // X of lower left corner of image
		6       WORD    YOffset;     // Y of lower left corner of image
		8       BYTE    PlaneInfo;   // Number of planes and bitdepth
		9       BYTE    PaletteFlag; // Color palette/video flag
		10      BYTE    VideoMode;   // Video mode of image
		11      WORD    PaletteType; // Type of color palette
		13      WORD    PaletteSize; // Size of color palette
		15
	*/

	uint8_t buf[15];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return lib_unexpected_eof;
	}

	desc->r = (struct raster_desc) {
		.w = buf_endian16(buf, little_endian),
		.h = buf_endian16(buf + 2, little_endian),
		.ch = (buf[8] >> 4) + 1,
		.bitdepth = buf[8] & 0x0f,
	};
	desc->x = buf_endian16(buf + 4, little_endian);
	desc->y = buf_endian16(buf + 6, little_endian);

	if (!desc->r.w || !desc->r.h) {
		return lib_invalid_header;
	}

	switch (desc->r.ch) {
	case 1: case 2: case 3: case 4:
		break;
	default:
		return lib_invalid_header;
	}

	switch (desc->r.bitdepth) {
	case 1: case 8:
		break;
	case 2: case 4:
		if (desc->r.ch != 1) {
			return lib_invalid_header;
		}
		break;
	default:
		return lib_invalid_header;
	}

	if (buf[9] == 0xff) {
		desc->video_mode = (char)buf[10];
		desc->pal_enabled = true;
		desc->pal_type = buf_endian16(buf + 11, little_endian);
		const uint16_t size = buf_endian16(buf + 13, little_endian);
		const enum lib_fail status = load_palette(desc, size);
		if (status != lib_ok) {
			return status;
		}
	}

	fread(&desc->blocks, 1, sizeof(desc->blocks), desc->ifp);
	desc->blocks = endian16(desc->blocks, little_endian);
	raster_normalize(&desc->r);
	return lib_ok;
}

enum lib_fail pictor_open_file(FILE *ifp, struct pictor_desc *desc) {
	const uint8_t magic[] = {0x34, 0x12};
	uint8_t id[sizeof(magic)];
	if (fread(id, 1, sizeof(id), ifp) == sizeof(id)) {
		if (!memcmp(id, magic, sizeof(id))) {
			desc->ifp = ifp;
			desc->r.palette = NULL;
			desc->pal_enabled = false;
			return lib_ok;
		}
		return lib_unknown_format;
	}
	return lib_unexpected_eof;
}
