#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "common.h"
#include "common_unpack.h"
#include "common_lib.h"

#include "lib_pcx.h"

// Disclaimer: I hate this format.

static const unsigned char default_ega[16*3] = {
	0x00, 0x00, 0x00,
	0x00, 0x00, 0xAA,
	0x00, 0xAA, 0x00,
	0x00, 0xAA, 0xAA,
	0xAA, 0x00, 0x00,
	0xAA, 0x00, 0xAA,
	0xAA, 0x55, 0x00,
	0xAA, 0xAA, 0xAA,

	0x55, 0x55, 0x55,
	0x55, 0x55, 0xFF,
	0x55, 0xFF, 0x55,
	0x55, 0xFF, 0xFF,
	0xFF, 0x55, 0x55,
	0xFF, 0x55, 0xFF,
	0xFF, 0xFF, 0x55,
	0xFF, 0xFF, 0xFF,
};

static void palcpy(struct colormap *pal, const unsigned char *restrict src) {
	memcpy(pal, src, 3);
	pal->a = 0xff;
}

static unsigned char * expand_data(const struct pcx_desc *desc,
unsigned char *restrict data, const unsigned char *restrict palette) {
	const bool unpacked = desc->bitdepth == 1;
	if (palette && desc->expand_pal) {
		if (desc->bitdepth == 8 && desc->planes > 1) {
			return data;
		}

		unsigned char *out = malloc(desc->w * desc->h * 3);
		if (!out) {
			free(data);
			return NULL;
		}

		const unsigned char bitdepth = unpacked ? 8 : desc->bitdepth;
		strip_colormap(out, data, palette, desc->w, desc->h, 1,
			desc->planes, bitdepth);
		free(data);
		return out;
	}
	return data;
}

static unsigned char * unpack_interleave_data(const struct pcx_desc *desc,
unsigned char *restrict data, const unsigned char *restrict palette) {
	unsigned char *out = malloc(desc->w * desc->planes * desc->h);
	if (!out) {
		free(data);
		return NULL;
	}

	const size_t planes = desc->planes;
	const size_t scanline = desc->bytes_per_line;

	const size_t inrow = scanline * planes;
	switch (desc->bitdepth) {
	case 1:
		for (size_t y = 0; y < desc->h; ++y) {
			const unsigned char *restrict src = data + y*inrow;
			unsigned char *restrict dst = out + y*desc->w;

			for (size_t x = 0; x < desc->w; ++x) {
				int val = 0;
				for (size_t ch = 0; ch < planes; ++ch) {
					const int byte = src[x/8 + ch*scanline];
					val |= (bool)(byte & (0x80 >> (x%8))) << ch;
				}
				dst[x] = (unsigned char)val;
			}
		}
		break;
	case 2:
	case 4:
		; const size_t diff = scanline
			- scanline_length(desc->w, desc->bitdepth, 1) + 1;
		strip_unpack(out, data, desc->w, desc->h, (unsigned char)diff,
			unpack, desc->bitdepth);
		break;
	case 8:
		; const size_t outrow = desc->w * planes;
		for (size_t y = 0; y < desc->h; ++y) {
			const unsigned char *restrict src = data + y*inrow;
			unsigned char *restrict dst = out + y*outrow;
			for (size_t x = 0; x < desc->w; ++x) {
				for (size_t ch = 0; ch < planes; ++ch) {
					dst[x*planes + ch] = src[x + ch*scanline];
				}
			}
		}
		break;
	default:
		free(data);
		free(out);
		return NULL;
	}
	free(data);
	return expand_data(desc, out, palette);
}

static unsigned char * load_palette(const struct pcx_desc *desc,
const unsigned char *restrict pal_src) {
	// Take a deep breath...
	struct colormap *pal = malloc(256 * sizeof(*pal));
	if (!pal) {
		return NULL;
	}

	const unsigned char *file_pal = desc->file_pal;
	const int entries = 1 << (desc->bitdepth * desc->planes);
	/* How to tell if we are in CGA mode? Weasels say you have to keep a
	 * table of bitdepth/planes combinations where equivalent values may
	 * tell you something, or that files with CGA resolution (width/height
	 * combinations that are actually likelier than usual anyway) might be
	 * in CGA mode. Whichever one you pick, your decoder will fail on some
	 * sample. */
	if (entries == 2) {
/*		const int fg_cga = file_pal[0] >> 4;
		palcpy(pal, default_ega + fg_cga*3);
		pal[1] = (struct colormap)
			{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff};*/
		pal[0] = (struct colormap){0x00, 0x00, 0x00, 0xff};
		pal[1] = (struct colormap){0xff, 0xff, 0xff, 0xff};
//	} else if (desc->cga_mode && entries == 4) {
	} else if (desc->bitdepth == 2 && desc->planes == 1) {
		int palnum, intensity;
		bool colorburst;
		if (desc->palette_type) {
			intensity = (imax(file_pal[4], file_pal[5]) > 200);
			palnum = file_pal[4] > file_pal[5] ? 0 : 1;
			colorburst = true;
		} else {
			const int status = file_pal[3] >> 5;
			intensity = status & 0x01;
			palnum = (status >> 1) & 0x01;
			colorburst = (status >> 2) & 0x01;
		}

		const int bg = file_pal[0] >> 4;
		palcpy(pal, default_ega + bg*3);
		if (colorburst) {
			const int offset = palnum + intensity*8;
			for (int i = 1; i < 4; ++i) {
				const int idx = offset + i*2;
				palcpy(pal + i, default_ega + idx * 3);
			}
		} else {
			// Tables? Who needs tables
			const int offset = intensity*8;
			for (int i = 1; i < 4; ++i) {
				// Fancy way of indexing '{3, 4, 7}'
				const int idx = offset + i*2 + (i & 1);
				palcpy(pal + i, default_ega + idx * 3);
			}
		}
	} else {
		for (int i = 0; i < entries; ++i) {
			palcpy(pal + i, pal_src + i*3);
		}
	}

	// Just to be safe with BW
//	if (entries == 2 && !memcmp(pal, pal + 1, 3)) {
//	}

	return (unsigned char *)pal;
}

static ptrdiff_t rle_decode(unsigned char *restrict out,
const unsigned char *restrict outlimit, const unsigned char *restrict buf,
const unsigned char *restrict buflimit) {
	do {
		const unsigned char packet = *buf;
		++buf;
		if (packet >= 192) {
			const size_t run_len = zumin(packet & 0x3f,
				(size_t)(outlimit - out));
			if (buf == buflimit) {
				break;
			}
			memset(out, *buf, run_len);
			out += run_len;
			++buf;
		} else {
			*out = packet;
			++out;
		}
	} while (out != outlimit && buf != buflimit);
	return buflimit - buf;
}

unsigned char * pcx_decode(const struct pcx_desc *desc,
unsigned char *restrict *palette) {
	*palette = NULL;

	fseek(desc->ifp, 0, SEEK_END);
	const long rlesize = ftell(desc->ifp) - 128;
	if (rlesize <= 0) {
		return NULL;
	}

	const size_t indims = desc->bytes_per_line * desc->planes * desc->h;
	unsigned char *data = malloc(indims);
	if (!data) {
		return NULL;
	}

	const size_t bufsize = zumin(indims*2, (size_t)rlesize);
	unsigned char *buf = malloc(bufsize);
	if (!buf) {
		free(data);
		return NULL;
	}

	fseek(desc->ifp, 128, SEEK_SET);
	const size_t read = fread(buf, 1, bufsize, desc->ifp);
	if (read < bufsize) {
		puts(RASTER_EOF);
	}

	const unsigned char *restrict bufend = buf + read;
	const ptrdiff_t remaining = rle_decode(data, data + indims, buf,
		bufend);

	const unsigned char *pal_src = NULL;
	const ptrdiff_t vga_len = 256*3;
	if (desc->bitdepth < 8) {
		switch (desc->version) {
		case pcx_ver25:
		case pcx_ver28_nopal:
			pal_src = default_ega;
			break;
		default:
			if (desc->bitdepth == 2 && desc->planes == 1) {
				pal_src = default_ega;
			} else {
				pal_src = desc->file_pal;
			}
		}
	} else if (desc->planes == 1 && remaining > vga_len) {
		if (*(bufend - remaining) == 0x0c) {
			pal_src = bufend - remaining + 1;
		}
	}

	if (pal_src) {
		*palette = load_palette(desc, pal_src);
		if (*palette == NULL) { // Alloc error
			free(buf);
			free(data);
			return NULL;
		}
	}
	free(buf);
	return unpack_interleave_data(desc, data, *palette);
}

static enum lib_fail validate_header(struct pcx_desc *desc,
const int width, const int height, const unsigned char planes,
const unsigned int bytes_per_line, const bool palette_type) {
	if (width < 1 || height < 1) {
		return lib_invalid_header;
	}

	switch (planes) {
	case 1: case 2: case 3: case 4:
		break;
	default:
		return lib_invalid_header;
	}


	switch (desc->bitdepth) {
	case 1: case 8:
		break;
	case 2: case 4:
		if (planes != 1) {
			return lib_unknown_format;
		}
		break;
	default:
		return lib_invalid_header;
	}

	desc->w = (unsigned)width;
	desc->h = (unsigned)height;
	desc->planes = planes;
	desc->bytes_per_line = bytes_per_line;
	desc->palette_type = palette_type;

	const size_t min_scanline = scanline_length(desc->w, desc->bitdepth, 1);
	if (bytes_per_line < min_scanline) {
		return lib_invalid_header;
	}
	return lib_ok;
}

enum lib_fail pcx_read_header(struct pcx_desc *desc) {
	/* Header continuation
		Offset  Size    Name
		0       WORD    XStart;
		2       WORD    YStart;
		4       WORD    XEnd;
		6       WORD    YEnd;
		8       WORD    HorzRes;
		10      WORD    VertRes;
		12      BYTE    EGAPalette[48];
		60      BYTE    Reserved1;
		61      BYTE    NumBitPlanes;
		62      WORD    BytesPerLine;
		64      WORD    PaletteType;
		66      WORD    HorzScreenSize; // [*]
		68      WORD    VertScreenSize; // [*]
		70      BYTE    Reserved2[54];
		124

	[*] Might be part of Reserved2 depending on the version.
	*/

	unsigned char header1[12];
	unsigned char header2[6];
	size_t read = fread(header1, 1, sizeof(header1), desc->ifp);
	read += fread(desc->file_pal, 1, sizeof(desc->file_pal), desc->ifp);
	read += fread(header2, 1, sizeof(header2), desc->ifp);
	if (read < sizeof(header1) + sizeof(desc->file_pal) + sizeof(header2)) {
		return lib_unexpected_eof;
	}

	const int xstart = buf_endian16(header1, little_endian);
	const int ystart = buf_endian16(header1 + 2, little_endian);
	const int xend = buf_endian16(header1 + 4, little_endian);
	const int yend = buf_endian16(header1 + 6, little_endian);
	const int width = xend - xstart + 1;
	const int height = yend - ystart + 1;

	return validate_header(desc, width, height,
		header2[1],
		buf_endian16(header2 + 2, little_endian),
		buf_endian16(header2 + 4, little_endian));
}

enum lib_fail pcx_open_file(FILE *ifp, struct pcx_desc *desc) {
	/* Header bytes for testing:
		Offset  Size    Name
		0	BYTE	IdentifierByte; // Always 0x0A
		1	BYTE	Version;
		2	BYTE	Encoding;       // Always 1
		3       BYTE    BitsPerPixel;
		4
	*/

	desc->cga_mode = false;
	desc->expand_pal = true;
	unsigned char sig[4];
	const size_t read = fread(sig, 1, sizeof(sig), ifp);
	if (read == sizeof(sig)) {
		if (sig[0] == 0x0a && sig[2] == 1) {
			switch (sig[1]) {
			case pcx_ver25:
			case pcx_ver28_egapal:
			case pcx_ver28_nopal:
			case pcx_paintbrush:
			case pcx_ver30:
				desc->version = sig[1];
				desc->bitdepth = sig[3];
				desc->ifp = ifp;
				return lib_ok;
			}
		}
		return lib_unknown_format;
	}
	return lib_unexpected_eof;
}

struct dcx_desc * dcx_read_offsets(FILE *ifp) {
	struct dcx_desc *desc = malloc(sizeof(*desc));
	if (desc) {
		fseek(ifp, 0, SEEK_END);
		const u_int32_t endsize = (u_int32_t)zumin(0xffffffff,
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
	/* DCX header:
		Offset  Size    Name
		0       DWORD   Identifier;  // 0xb1 0x68 0xde 0x3a
		4       DWORD   PageTable[]; // 0 terminated, max 1024;
	*/

	const unsigned char dcx_magic[4] = {0xb1, 0x68, 0xde, 0x3a};
	unsigned char sig[4];
	const size_t read = fread(sig, 1, sizeof(sig), ifp);
	if (read == sizeof(sig)) {
		if (!memcmp(dcx_magic, sig, sizeof(sig))) {
			return lib_ok;
		}
		return lib_unknown_format;
	}
	return lib_unexpected_eof;
}
