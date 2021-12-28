#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "../common.h"
#include "../raster/unpack.h"
#include "../raster/graphics_adapters.h"
#include "../raster/raster.h"

#include "pcx.h"

// Disclaimer: I hate this format.

static const size_t RLE_MAX_RUN = 0x3f;
static const size_t VGA_PAL_LEN = 256*3;

enum pcx_palette_source {
	pcx_no_pal,
	pcx_ega,
	pcx_file_header,
	pcx_vga,
};

const char * pcx_version_string(const enum pcx_version ver) {
	switch (ver) {
	case pcx_ver25: return "2.5";
	case pcx_ver28_egapal: return "2.8 with EGA palette";
	case pcx_ver28_nopal: return "2.8 with no palette";
	case pcx_paintbrush: return "Paintbrush for Windows";
	case pcx_ver30: return "3.0";
	}
	return "Unknown version";
}
__attribute__((unused))
static unsigned char * pcx_unpack_interleave(unsigned char *restrict src,
struct raster_desc *desc) {
	size_t comps = 1;
	if (!desc->palette) {
		comps = desc->ch;
	}

	const size_t dims = desc->w * desc->h * comps;
	unsigned char *dst = malloc(dims);
	if (!dst) {
		free(src);
		return NULL;
	}

	const size_t scanline = scanline_length(desc->w, desc->bitdepth,
		desc->alignment);
	for (size_t y = 0; y < desc->h; ++y) {
		vga_interleave(dst + y * desc->w * comps,
			src + y * scanline * desc->ch, desc, 1, scanline);
	}
	free(src);

	if (desc->palette) {
		desc->ch = 1;
		desc->bitdepth = 8;
	}
	desc->alignment = 1;
	return dst;
}

static bool check_cga_mode(const struct pcx_desc *desc) {
	/* Files with bitdepth == 2 can be in CGA mode, meaning bytes in the
	 * first two entries of the header palette are used to select one of
	 * CGA palettes, or can be in 'regular' mode, in which the header
	 * palette is a regular palette like with higher bitdepths. This being
	 * the PCX format means there's no indication of when was each meant to
	 * be used, and plenty of samples relying on either behaviour.

	 * Although some docs talk about strange heuristics like checking for
	 * common CGA resolutions or bits/planes combinations, I believe the
	 * cowboy programmers who made this mess in the first place expected us
	 * to realize that, if CGA mode needs only 2 out of 4 entries, the
	 * other two were obviously going to be be zeroed. Checking this makes
	 * all the images in the FFmpeg samples display OK, as far as one can
	 * tell with a format that is undecidable to render.

	 * Care should be taken, however, NOT to check the rest of the palette
	 * area for zeroes, as them cowboy programmers will also merrily leave
	 * arbitrary data in there, either because of uncleared memory or as a
	 * sort of not-backwards-incompatible header extension. */
	return desc->r.bitdepth == 2 && !memchk(desc->file_pal + 6, 0, 6);
}

static struct raster_pal * load_palette(const struct pcx_desc *desc,
const struct pix_rgb8 *pal_data) {
	// Take a deep breath...
	struct raster_pal *pal = malloc(sizeof(*pal));
	if (!pal) {
		return NULL;
	}

	const size_t entries = desc->entries;
	const unsigned char *file_pal = desc->file_pal;
	if (entries == 2) {
		/* PC Paintbrush will display a dialog asking the user whether
		 * to open 1-bit 1-plane files as B&W or using the header
		 * palette. So one shouldn't get too stressed about which way
		 * is correct.
		 * Here, to use the file palette, we check if
		 *  · the file version allows a palette
		 *  · palette_type is non-zero
		 *  · entries are different
		 * This seems to work rather well for all samples. */
		if (pal_data && desc->palette_type
		&& memcmp(pal_data, pal_data + 1, sizeof(*pal_data))) {
			pix_rgb8_to_rgba8(pal->color, pal_data, entries);
		} else {
			/* Beware when testing: imagemagick renders monochrome
			 * opposite from ffmpeg. */
			pal->color[0] = (struct pix_rgba8){0x00, 0x00, 0x00, 0xff};
			pal->color[1] = (struct pix_rgba8){0xff, 0xff, 0xff, 0xff};
		}
	} else if (check_cga_mode(desc)) {
		unsigned palnum;
		bool intensity, colorburst;
		if (desc->palette_type) {
			intensity = imax(file_pal[4], file_pal[5]) > 200;
			palnum = file_pal[4] > file_pal[5] ? 0 : 1;
			colorburst = true;
		} else {
			const int status = file_pal[3];
			intensity = (status >> 5) & 1;
			palnum = (status >> 6) & 1;
			colorburst = (status >> 7) & 1;
		}

		const uint8_t cga_bg = file_pal[0] >> 4;
		pal->color[0] = cga_palette(cga_bg);
		if (colorburst) {
			// Palettes 0 and 1
			const size_t offset = palnum + intensity*8;
			for (size_t i = 1; i < 4; ++i) {
				const size_t idx = offset + i*2;
				pal->color[i] = cga_palette(idx);
			}
		} else {
			// Unofficial palette 2
			const size_t offset = intensity*8;
			for (size_t i = 1; i < 4; ++i) {
				// Fancy way of indexing '{3, 4, 7}'
				const size_t idx = offset + i*2 + (i & 1);
				pal->color[i] = cga_palette(idx);
			}
		}
	} else if (pal_data) { // Header or trailing palette
		pix_rgb8_to_rgba8(pal->color, pal_data, entries);
	} else { // Standard EGA palette (CGA)
		for (size_t i = 0; i < entries; ++i) {
			pal->color[i] = cga_palette(i);
		}
	}
	return pal;
}

static enum lib_fail looking_for_lost_pauline(struct pcx_desc *desc,
const unsigned char *restrict rle_end, const size_t rle_remaining) {
	if (desc->r.bitdepth > 1 && desc->r.ch > 1) {
		return lib_ok;
	}

	enum pcx_palette_source pal_src = pcx_no_pal;
	const unsigned char *vga_id = rle_end - rle_remaining;
	if (desc->r.bitdepth == 8) {
		/* Beware: Some internet weasels say the magic byte is 0xC0,
		 * but it's actually 0x0C */
		if (rle_remaining > VGA_PAL_LEN && *vga_id == 0x0c) {
			pal_src = pcx_vga;
		}
	} else {
		switch (desc->version) {
		case pcx_ver25:
		case pcx_ver28_nopal:
			pal_src = pcx_ega;
			break;
		default:
			pal_src = pcx_file_header;
		}
	}

	const unsigned char *pal_data = NULL;
	switch (pal_src) {
	case pcx_no_pal:
		return lib_ok;
	case pcx_ega:
		break;
	case pcx_file_header:
		pal_data = desc->file_pal;
		break;
	case pcx_vga:
		pal_data = vga_id + 1;
		break;
	}

	desc->r.palette = load_palette(desc, (struct pix_rgb8 *)pal_data);
	if (desc->r.palette == NULL) {
		return lib_alloc_error;
	}
	return lib_ok;
}

static size_t rle_decode(unsigned char *restrict dst, const size_t dst_len,
const unsigned char *restrict rle, const size_t rle_len) {
	size_t d = 0;
	size_t r = 0;
	const uint8_t mask = 0xc0;
	while (d < dst_len && r < rle_len) {
		const unsigned char packet = rle[r];
		++r;
		if (packet >= mask) {
			if (r == rle_len) {
				break;
			}
			const size_t run_len = packet - mask;
			memset(dst + d, rle[r], run_len);
			d += run_len;
			++r;
		} else {
			dst[d] = packet;
			++d;
		}
	}
	return rle_len - r;
}

unsigned char * pcx_decode(struct pcx_desc *desc) {
	const size_t dims = desc->bytes_per_line * desc->r.ch * desc->r.h;
	// Add padding to save on a range check.
	unsigned char *data = malloc(dims + RLE_MAX_RUN);
	if (!data) {
		return NULL;
	}

	const size_t rle_len = zumin(dims*2 + VGA_PAL_LEN, (size_t)desc->rle_len);
	unsigned char *rle = malloc(rle_len);
	if (!rle) {
		free(data);
		return NULL;
	}

	const size_t read = fread(rle, 1, rle_len, desc->ifp);
	if (read < rle_len) {
		puts(RASTER_EOF);
	}

	const size_t remaining = rle_decode(data, dims, rle, read);

	const enum lib_fail fail = looking_for_lost_pauline(desc, rle + read,
		remaining);
	free(rle);
	if (fail != lib_ok) {
		free(data);
		return NULL;
	}

	if (desc->r.ch > 1) {
		data = pcx_unpack_interleave(data, &desc->r);
	}
	raster_normalize(&desc->r);
	return data;
}

static enum lib_fail validate_header(struct pcx_desc *desc,
const uint8_t bitdepth, const int width, const int height,
const uint8_t planes, const uint16_t bytes_per_line,
const uint16_t palette_type) {
	switch (bitdepth) {
	case 1: case 2: case 4: case 8:
		break;
	default:
		return lib_invalid_header;
	}

	if (width < 1 || height < 1) {
		return lib_invalid_header;
	}
	if (planes < 1 || planes > 4) {
		return lib_invalid_header;
	}

	desc->r = (struct raster_desc) {
		.w = (unsigned)width,
		.h = (unsigned)height,
		.ch = planes,
		.bitdepth = bitdepth,
	};
	const size_t diff = bytes_per_line - scanline_length(desc->r.w,
		desc->r.bitdepth, 1);
	size_t align = 1;
	if (diff) {
		align = 1 << (zulog2(diff) + 1);
		if (align > 4) {
			return lib_invalid_header;
		}
	}
	desc->r.alignment = (uint8_t)align;
	desc->bytes_per_line = bytes_per_line;
	desc->palette_type = palette_type;
	desc->entries = 1 << (desc->r.bitdepth * desc->r.ch);
	return lib_ok;
}

enum lib_fail pcx_read_header(struct pcx_desc *desc) {
	/* Header continuation
		Offset  Size    Name
		0       BYTE    BitsPerPixel;   // 1, 2, 4, or 8
		1       WORD    XStart;
		3       WORD    YStart;
		5       WORD    XEnd;
		7       WORD    YEnd;
		9       WORD    HorzRes;
		11      WORD    VertRes;
		13      BYTE    EGAPalette[48];
		61      BYTE    Reserved1;
		62      BYTE    NumBitPlanes;   // 1, 2, 3, or 4
		63      WORD    BytesPerLine;   // Line of a single plane
		65      WORD    PaletteType;    // CGA palette interpretation. [1]
		67      WORD    HorzScreenSize; // [2]
		69      WORD    VertScreenSize; // [2]
		71      BYTE    Reserved2[54];
		125

	[1] Many docs claim it can only be 1 or 2, but apparently it was 0
		before PC Paintbrush 4.0, and any of the three afterwards.[3]
		What practical difference 1 or 2 make it's not yet clear to me.
		Non-CGA files meanwhile can have random values.
	[2] Fields added after version 4.0, previously part of Reserved2. [3]
	[3] As befitting this format, version 4 is just when the version field
		stopped being updated.
	*/

	uint8_t header1[13]; // Header from offset 0 to 13
	uint8_t header2[10]; // Header from offset 61 to 71
	size_t read = fread(header1, 1, sizeof(header1), desc->ifp);
	read += fread(desc->file_pal, 1, sizeof(desc->file_pal), desc->ifp);
	read += fread(header2, 1, sizeof(header2), desc->ifp);
	if (read < sizeof(header1) + sizeof(desc->file_pal) + sizeof(header2)) {
		return lib_unexpected_eof;
	}
	fseek(desc->ifp, 125 - 71, SEEK_CUR);

	const int xstart = buf_endian16(header1 + 1, little_endian);
	const int ystart = buf_endian16(header1 + 3, little_endian);
	const int xend = buf_endian16(header1 + 5, little_endian);
	const int yend = buf_endian16(header1 + 7, little_endian);
	const int width = xend - xstart + 1;
	const int height = yend - ystart + 1;

	desc->horz_res = buf_endian16(header1 + 9, little_endian);
	desc->vert_res = buf_endian16(header1 + 11, little_endian);
	desc->horz_screen = buf_endian16(header2 + 6, little_endian);
	desc->vert_screen = buf_endian16(header2 + 8, little_endian);
	return validate_header(desc, header1[0], width, height,
		header2[1],
		buf_endian16(header2 + 2, little_endian),
		buf_endian16(header2 + 4, little_endian));
}

enum lib_fail pcx_open_file(FILE *ifp, struct pcx_desc *desc,
long file_len) {
	/* Header bytes for testing:
		Offset  Size    Name
		0	BYTE	IdentifierByte; // Always 0x0A
		1	BYTE	Version;
		2	BYTE	Encoding;       // Always 1
		3
	*/

	if (!file_len) {
		file_len = file_get_remaining(ifp);
	}
	file_len -= 128;
	if (file_len > 0) {
		uint8_t sig[3];
		if (fread(sig, sizeof(sig), 1, ifp)) {
			if (sig[0] == 0x0a && sig[2] == 1) {
				switch (sig[1]) {
				case pcx_ver25:
				case pcx_ver28_egapal:
				case pcx_ver28_nopal:
				case pcx_paintbrush:
				case pcx_ver30:
					desc->version = sig[1];
					desc->ifp = ifp;
					desc->r.palette = NULL;
					desc->rle_len = file_len;
					return lib_ok;
				}
			}
			return lib_unknown_format;
		}
	}
	return lib_unexpected_eof;
}

struct dcx_desc * dcx_read_offsets(FILE *ifp) {
	struct dcx_desc *desc = malloc(sizeof(*desc));
	if (desc) {
		fseek(ifp, 0, SEEK_END);
		const uint32_t endsize = (uint32_t)zumin(UINT32_MAX,
			(size_t)ftell(ifp));

		fseek(ifp, 4, SEEK_SET);
		const size_t read = fread(desc->off, sizeof(*desc->off), 1023, ifp);
		size_t i = 0;
		if (read && desc->off[i]) {
			desc->off[i] = endian32(desc->off[i], little_endian);
			++i;
			while (i < read && desc->off[i] > desc->off[i-1]
			&& endsize > desc->off[i]) {
				desc->off[i] = endian32(desc->off[i], little_endian);
				desc->len[i-1] = desc->off[i] - desc->off[i-1];
				++i;
			}
			desc->len[i-1] = endsize - desc->off[i-1];
		}
		desc->nr = i;
	}
	return desc;
}

enum lib_fail dcx_open_file(FILE *ifp) {
	/* Why would anyone use the most device dependent file format ever for
	 * sending documents is beyond me. */

	/* DCX header:
		Offset  Size    Name
		0       DWORD   Identifier;  // 0xb1 0x68 0xde 0x3a
		4       DWORD   PageTable[]; // 0 terminated, max 1024;
	*/

	const uint8_t sig[] = {0xb1, 0x68, 0xde, 0x3a};
	return lib_sigcmp(sig, sizeof(sig), ifp);
}
