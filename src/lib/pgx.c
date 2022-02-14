#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../common.h"
#include "../raster/mem.h"
#include "pgx.h"

static const size_t LZSS_PAD = 2 * 8 + 1;

static void repeat_or_zero(uint8_t *unpack, size_t upos, const size_t offset,
size_t count) {
	if (offset > upos + count) {
		memset(unpack + upos, 0, count);
	} else {
		if (offset > upos) {
			memset(unpack + upos, 0, offset - upos);
			upos += offset - upos;
			count -= offset - upos;
		}
		memrepeat(unpack, upos, offset, count);
/*		while (count > offset) {
			memcpy(unpack + upos, unpack + upos - offset, offset);
			upos += offset;
			count -= offset;
		}
		memcpy(unpack + upos, unpack + upos - offset, count);*/
	}
}

static size_t lzss_decomp(uint8_t *restrict unpack, const size_t unpack_len,
const uint8_t *restrict pack, const size_t pack_len) {
	/* Not to be confused with the GML_ARC LZSS algorithm, which requires
	 * negating the input beforehand. */
	const uint_fast16_t dict_mask = 0xfff;
	size_t upos = 0;
	size_t ppos = 0;
	while (upos < unpack_len && ppos < pack_len) {
		uint8_t flags = pack[ppos];
		++ppos;
		for (size_t i = 0; i < 8; ++i, flags >>= 1) {
			if (flags & 1) {
				if (upos >= unpack_len) {
					break;
				}
				unpack[upos] = pack[ppos];
				++ppos;
				++upos;
			} else {
				const uint8_t first = pack[ppos];
				const uint8_t second = pack[ppos + 1];
				ppos += 2;

				const size_t dict_offset = (second & 0xf0U) << 4 | first;
				const size_t offset = (upos - 18 - dict_offset)
					& dict_mask;
				const size_t count = 18 - (second & 0x0f);
				if (upos + count >= unpack_len) {
					return upos;
				}
				repeat_or_zero(unpack, upos, offset, count);
				upos += count;
			}
		}
	}
	return upos;
}

size_t pgx_decode(const struct pgx_desc *desc, void *restrict dst) {
	fseek(desc->ifp, -(long)(desc->comp_size), SEEK_END);
	size_t written = 0;
	uint8_t *comp = malloc(desc->comp_size + LZSS_PAD);
	if (comp) {
		const size_t dims = raster_size(&desc->rast);
		const size_t read = fread(comp, 1, desc->comp_size, desc->ifp);
		written = lzss_decomp(dst, dims, comp, read);
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
