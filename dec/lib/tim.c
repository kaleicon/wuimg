#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../common.h"

#include "tim.h"

void tim_cleanup(struct tim_desc *desc) {
	free(desc->clut.data);
}

unsigned char * tim_decode(const struct tim_desc *desc, bool expand) {
	if (desc->bitdepth == 24 || (desc->bitdepth == 8 && desc->clut.nb)) {
		expand = false;
	}

	const size_t src_len = (size_t)desc->line_len * 2 * desc->h;
	size_t out_len;
	if (expand) {
		out_len = desc->w * desc->h;
		if (desc->clut.data || desc->bitdepth == 16) {
			out_len *= 3;
		}
	} else {
		out_len = src_len;
	}

	unsigned char *out = malloc(out_len);
	if (!out) {
		return NULL;
	}

	void *restrict src = out + out_len - src_len;
	if (fread(src, 1, src_len, desc->ifp) != src_len) {
		free(out);
		return NULL;
	}

	if (expand) {
		if (desc->clut.data) {
			strip_colormap(out, src, desc->clut.data, desc->w,
				desc->h, 2, 3, desc->bitdepth);
		} else if (desc->bitdepth == 4) {
			const enum unpack_op op = desc->clut.nb ?
				op_unpack : op_expand;
			strip_unpack(out, src, desc->w, desc->h, 2, op,
				desc->bitdepth);
		} else if (desc->bitdepth == 16) {
			const uint16_t *restrict b = src;
			for (size_t i = 0; i < src_len/2; ++i) {
				pixel_expand555(out + i*3,
					endian16(b[i], little_endian));
			}
		}
	}
	return out;
}

struct colormap * tim_take_colormap(struct tim_desc *desc) {
	struct colormap *cm = desc->clut.data;
	desc->clut.data = NULL;
	return cm;
}

static enum lib_fail read_cluts(struct tim_desc *desc,
unsigned char *restrict header) {
	struct tim_clut *clut = &desc->clut;
	clut->x = buf_endian16(header + 4, little_endian);
	clut->y = buf_endian16(header + 6, little_endian);
	clut->nb = buf_endian16(header + 10, little_endian);
	if (!clut->nb) {
		return lib_invalid_header;
	}

	const size_t colors = 1 << desc->bitdepth;
	if (colors != buf_endian16(header + 8, little_endian)) {
		return lib_invalid_header;
	}

	clut->data = malloc(colors * clut->nb * sizeof(*clut->data));
	if (!clut->data) {
		return lib_alloc_error;
	}

	uint16_t buf[256];
	for (size_t n = 0; n < clut->nb; ++n) {
		if (fread(buf, 2, colors, desc->ifp) != colors) {
			return lib_unexpected_eof;
		}

		for (size_t i = 0; i < colors; ++i) {
			pixel_expand555(clut->data + n*colors + i,
				endian16(buf[i], little_endian));
			clut->data[i].a = 0xff;
		}
	}
	return lib_ok;
}

enum lib_fail tim_parse_header(struct tim_desc *desc) {
	/* TIM header (little-endian) (after id):
		Offset  Size    Name
		0       DWORD   Flags:

			Bits    Name
			0-2     Bitmap type:
				000: 4bpp
				001: 8bpp
				010: 16bpp
				011: 24bpp
				100: Mixed
			3       CLUT (a.k.a. palette):
				0: No CLUT
				1: Has CLUT
			4-      Reserved
		4

	* CLUT header, if present:
		Offset  Size    Name
		0       DWORD   SizeOfCLUT // Including this header
		4       WORD    PaletteOrigX
		6       WORD    PaletteOrigY
		8       WORD    NbOfColors // Always 2^bpp
		10      WORD    NbOfCLUTs
		12      VAR     CLUTData   // 16bit RGB555
		??

	* Image header:
		Offset  Size    Name
		0       DWORD   SizeOfImage // Including this header
		4       WORD    ImageOrigX
		6       WORD    ImageOrigY
		8       WORD    ImageWidth  // WORDs per line
		10      WORD    ImageHeight
		12      VAR     ImageData
	*/

	uint32_t flags;
	unsigned char header[12];
	if (fread(&flags, 1, sizeof(flags), desc->ifp) != sizeof(flags)) {
		return lib_unexpected_eof;
	}
	if (fread(header, 1, sizeof(header), desc->ifp) != sizeof(header)) {
		return lib_unexpected_eof;
	}

	flags = endian32(flags, little_endian);
	switch (flags & 0x7) {
	case 0: desc->bitdepth = 4; break;
	case 1: desc->bitdepth = 8; break;
	case 2: desc->bitdepth = 16; break;
	case 3: desc->bitdepth = 24; break;
	case 4: return lib_tim_mixed_bitdepth;
	default: return lib_invalid_header;
	}

	if (flags & 0x8) {
		if (desc->bitdepth > 8) {
			return lib_invalid_header;
		}

		const enum lib_fail status = read_cluts(desc, header);
		if (status != lib_ok) {
			return status;
		}
		if (fread(header, 1, sizeof(header), desc->ifp) != sizeof(header)) {
			return lib_unexpected_eof;
		}
	}

	desc->x = buf_endian16(header + 4, little_endian);
	desc->y = buf_endian16(header + 6, little_endian);
	desc->line_len = buf_endian16(header + 8, little_endian);
	desc->w = (unsigned short)(desc->line_len * 16 / desc->bitdepth);
	desc->h = buf_endian16(header + 10, little_endian);
	return lib_ok;
}

enum lib_fail tim_open_file(FILE *ifp, struct tim_desc *desc) {
	const unsigned char id[] = {0x10, 0, 0, 0};
	unsigned char buf[sizeof(id)];
	if (fread(buf, 1, sizeof(buf), ifp) == sizeof(buf)) {
		if (!memcmp(id, buf, sizeof(id))) {
			desc->ifp = ifp;
			desc->clut.data = NULL;
			desc->clut.nb = 0;
			return lib_ok;
		}
		return lib_invalid_signature;
	}
	return lib_unexpected_eof;
}
