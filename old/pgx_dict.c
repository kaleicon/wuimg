#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../common.h"
#include "../raster/lib.h"
#include "pgx.h"

static size_t lzss_decomp(uint8_t *restrict unpack, const size_t unpack_len,
const uint8_t *restrict pack, const size_t pack_len, uint8_t *restrict dict) {
	/* Not to be confused with the GML_ARC LZSS algorithm, which requires
	 * negating the input beforehand. */
	const uint_fast16_t dict_mask = 0xfff;
	uint_fast16_t dict_pos = 0xfee;

	size_t upos = 0;
	size_t ppos = 0;
	while (upos < unpack_len && ppos < pack_len - 1) {
		uint8_t flags = pack[ppos];
		++ppos;
		for (int i = 0; i < 8; ++i, flags >>= 1) {
			if (flags & 1) {
				if (ppos >= pack_len) {
					break;
				}
				unpack[upos] = pack[ppos];
				dict[dict_pos] = pack[ppos];

				dict_pos = (dict_pos + 1) & dict_mask;
				++ppos;
				++upos;
			} else {
				if (ppos >= pack_len - 1) {
					break;
				}
				const uint8_t first = pack[ppos];
				const uint8_t second = pack[ppos + 1] ^ 0x0f;
				ppos += 2;

				size_t dict_offset = (second & 0xf0U) << 4 | first;
				const int count = (second & 0x0fU) + 3;
				for (int i = 0; i < count && upos < unpack_len; ++i) {
					const uint8_t byte = dict[dict_offset];
					dict[dict_pos] = byte;
					unpack[upos] = byte;

					dict_offset = (dict_offset + 1) & dict_mask;
					dict_pos = (dict_pos + 1) & dict_mask;
					++upos;
				}
			}
		}
	}
	return upos;
}

size_t pgx_decode(const struct pgx_desc *desc, void *restrict dst) {
	fseek(desc->ifp, -(long)(desc->comp_size), SEEK_END);
	size_t written = 0;
	uint8_t *comp = malloc(desc->comp_size);
	if (comp) {
		uint8_t *dict = calloc(0x1000, 1);
		if (dict) {
			const size_t dims = raster_size(&desc->rast);
			const size_t read = fread(comp, 1, desc->comp_size,
				desc->ifp);
			written = lzss_decomp(dst, dims, comp, read, dict);
			free(dict);
		}
		free(comp);
	}
	return written;
}

enum lib_fail pgx_read_header(struct pgx_desc *desc) {
	/* PGX header (after signature):
		Offset  Size    Name
		0       BYTE[4] StartingBytes; // of compressed data
		4       DWORD   Width;
		8       DWORD   Height;
		12      WORD    IsTransparent;
		14      BYTE    ???;
		15      BYTE    ExtraData?;
		16      DWORD   CompressedSize;
		20      BYTE[8] Padding?;
		28

	 * If ExtraData is set, there's some encrypted metadata of unknown size
	 * between the header and the compressed stream. The expected way
	 * to skip it seems to be searching for StartingBytes. The convenient
	 * way is to seek to -CompressedSize bytes from the end of the file.
	*/

	fseek(desc->ifp, 4, SEEK_CUR);
	uint8_t buf[16];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	desc->rast = (struct raster_desc) {
		.w = buf_endian32(buf, little_endian),
		.h = buf_endian32(buf + 4, little_endian),
		.ch = 4,
		.bitdepth = 8,
		.layout = pix_bgra,
	};
	desc->transparent = buf_endian16(buf + 8, little_endian);
	desc->comp_size = buf_endian32(buf + 12, little_endian);
	if (!desc->rast.w || !desc->rast.h) {
		return lib_invalid_header;
	}
	raster_normalize(&desc->rast);
	return lib_ok;
}

enum lib_fail pgx_open_file(struct pgx_desc *desc, FILE *ifp) {
	const unsigned char sig[] = {'P', 'G', 'X', 0};
	const enum lib_fail st = lib_sigcmp(sig, sizeof(sig), ifp);
	if (st == lib_ok) {
		desc->ifp = ifp;
	}
	return st;
}
