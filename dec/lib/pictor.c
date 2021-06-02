#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#include "common/lib.h"
#include "common/unpack.h"
#include "common/graphics_adapters.h"
#include "../../common.h"

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
	free(desc->palette.pal);
}

static unsigned char * interleave_data(const struct pictor_desc *desc,
unsigned char *restrict planes, const size_t outlen) {
	if (desc->planes == 1) {
		return planes;
	}

	unsigned char *out = malloc(outlen);
	if (!out) {
		free(planes);
		return NULL;
	}

	strip_interleave(out, planes, desc->w, desc->planes, 8);
	free(planes);
	return out;
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
		 * includes its own size and it was already read, so we end up
		 * getting the _next_ block size at the end of the buffer.
		 * This is intentional. */
		const size_t read = fread(block, 1, block_size, ifp);
		if (read < min_size - 2) {
			break;
		}

		bool last_block;
		size_t rem;
		if (read == block_size) {
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
				if (rem <= k + 1) {
					break;
				}

				size_t count = block[k];
				++k;
				if (!count) {
					if (rem <= k + 2) {
						break;
					}
					count = buf_endian16(block + k,
						little_endian);
					k += 2;
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

unsigned char * pictor_decode(const struct pictor_desc *desc) {
	uint16_t buf;
	if (!fread(&buf, sizeof(buf), 1, desc->ifp)) {
		return NULL;
	}

	const size_t outdims = desc->w * desc->planes * desc->h;
	unsigned char *out = malloc(outdims);
	if (!out) {
		return NULL;
	}

	const size_t raster_len = scanline_length(desc->w, desc->bitdepth, 1)
		* desc->planes * desc->h;
	unsigned char *raster = out + outdims - raster_len;

	const bool blocks = endian16(buf, little_endian);
	size_t written;
	if (blocks) {
		written = rle_decode(desc->ifp, raster, raster_len);
	} else {
		written = fread(raster, 1, raster_len, desc->ifp);
	}

	if (!written) {
		free(out);
		return NULL;
	} else if (written < raster_len) {
		puts(RASTER_EOF);
	}

	if (desc->bitdepth < 8) {
		const enum unpack_op op = desc->palette.enabled
			? op_unpack : op_expand;
		strip_unpack(out, raster, desc->w, desc->h * desc->planes, 1,
			op, desc->bitdepth);
	}

	return interleave_data(desc, out, outdims);
}

void * pictor_take_palette(struct pictor_desc *desc) {
	struct colormap *pal = desc->palette.pal;
	desc->palette.pal = NULL;
	return pal;
}

static enum lib_fail load_palette(struct pictor_desc *desc) {
	struct pictor_palette *pal = &desc->palette;
	const int bpp = desc->planes * desc->bitdepth;

	printf("entries: %u, type: %u\n", pal->size, pal->type);
	switch (pal->type) {
	case no_palette:
		if (bpp == 1 || bpp > 4) {
			return lib_ok;
		}
		break;
	case cga_palette:
		if (pal->size != 2) {
			return lib_invalid_header;
		}
		break;
	case pcjr_palette:
	case ega_palette:
		if (pal->size != 16) {
			return lib_invalid_header;
		}
		break;
	case vga_palette:
	// No docs as to what this is supposed to mean
	case vga_too_i_think:
		break;
	default:
		return lib_invalid_header;
	}

	const size_t size = sizeof(*pal->pal) * 256;
	pal->pal = malloc(size);
	if (!pal->pal) {
		return lib_alloc_error;
	}

	unsigned char *buf = (unsigned char *)(pal->pal) + size - pal->size;
	if (fread(buf, 1, pal->size, desc->ifp) != pal->size) {
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
	switch (pal->type) {
	case no_palette:
		if (bpp == 2) {
			for (size_t i = 0; i < 4; ++i) {
				pal->pal[i] = lib_cga_palette(cga_modes[0][i]);
			}
		} else if (bpp == 4) {
			for (size_t i = 0; i < 16; ++i) {
				pal->pal[i] = lib_cga_palette(i);
			}
		}
		break;
	case cga_palette:
		if (buf[0] >= 6 || buf[1] >= 16) {
			return lib_invalid_header;
		}
		const int mode = buf[0];

		pal->pal[0] = lib_cga_palette(buf[1]);
		for (size_t i = 1; i < 4; ++i) {
			pal->pal[i] = lib_cga_palette(cga_modes[mode][i]);
		}
		break;
	case pcjr_palette:
	case ega_palette:
		for (size_t i = 0; i < zumin(pal->size, 16); ++i) {
			pal->pal[i] = lib_cga_palette(buf[i]);
		}
		break;
	case vga_palette:
	case vga_too_i_think:
		; const unsigned maxval = (1 << 6) - 1;
		const unsigned scale = (UCHAR_MAX << 8) / maxval + 1;
		for (size_t i = 0; i < zumin(pal->size / 3, 256); ++i) {
			pal->pal[i].r = (unsigned char)((buf[i*3] * scale) >> 8);
			pal->pal[i].g = (unsigned char)((buf[i*3+1] * scale) >> 8);
			pal->pal[i].b = (unsigned char)((buf[i*3+2] * scale) >> 8);
			pal->pal[i].a = 0xff;
		}
		break;
	}
	pal->enabled = true;
	return lib_ok;
}

enum lib_fail pictor_read_header(struct pictor_desc *desc) {
	/* Pictor file header (after id):
		Offset  Size    Name
		0       WORD    Width;
		2       WORD    Height;
		4       WORD    XOffset;     // X of lower left corner of image
		6       WORD    YOffset;     // Y of lower left corner of image
		8       BYTE    PlaneInfo;   // BPP and number color planes
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

	desc->w = buf_endian16(buf, little_endian);
	desc->h = buf_endian16(buf + 2, little_endian);
	desc->x = buf_endian16(buf + 4, little_endian);
	desc->y = buf_endian16(buf + 6, little_endian);

	desc->bitdepth = buf[8] & 0x0f;
	desc->planes = (buf[8] >> 4) + 1;

	if (!desc->w || !desc->h) {
		return lib_invalid_header;
	}

	switch (desc->bitdepth) {
	case 1: case 2: case 4: case 8:
		break;
	default:
		return lib_invalid_header;
	}

	switch (desc->planes) {
	case 1: case 2: case 3: case 4:
		break;
	default:
		return lib_invalid_header;
	}

	if (buf[9] == 0xff) {
		desc->video_mode = (char)buf[10];
		desc->palette.type = buf_endian16(buf + 11, little_endian);
		desc->palette.size = buf_endian16(buf + 13, little_endian);
		return load_palette(desc);
	}
	return lib_ok;
}

enum lib_fail pictor_open_file(FILE *ifp, struct pictor_desc *desc) {
	uint16_t id;
	if (fread(&id, 1, sizeof(id), ifp) == sizeof(id)) {
		const unsigned char magic[] = {0x34, 0x12};
		if (!memcmp(&id, magic, sizeof(id))) {
			desc->ifp = ifp;
			desc->palette.enabled = false;
			desc->palette.pal = NULL;
			return lib_ok;
		}
		return lib_unknown_format;
	}
	return lib_unexpected_eof;
}
