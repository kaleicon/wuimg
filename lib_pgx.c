#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "common.h"
#include "common_lib.h"
#include "lib_pgx.h"

static size_t lzss_decomp(uint8_t *restrict unpack, const size_t unpack_len,
const uint8_t *restrict pack, const size_t pack_len) {
	/* Not to be confused with the GML_ARC LZSS algorithm, which requires
	 * negating the input beforehand. */
	uint8_t *restrict dict = calloc(0x1000, 1);
	if (!dict) {
		return 0;
	}

	const uint_fast16_t dict_mask = 0xfff;
	uint_fast16_t dict_pos = 0xfee;
	uint_fast16_t mask = 0x100;
	uint_fast16_t flags = 1;

	size_t upos = 0;
	size_t ppos = 0;
	while (upos < unpack_len) {
		if (mask & 0x100) {
			if (ppos >= pack_len) {
				break;
			}
			flags = pack[ppos];
			mask = 1;
			++ppos;
		}

		if (flags & mask) {
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
			const size_t count = (second & 0x0f) + 3;
			for (size_t i = 0; i < count && upos < unpack_len; ++i) {
				const uint8_t byte = dict[dict_offset];
				dict[dict_pos] = byte;
				unpack[upos] = byte;

				dict_offset = (dict_offset + 1) & dict_mask;
				dict_pos = (dict_pos + 1) & dict_mask;
				++upos;
			}
		}
		mask <<= 1;
	}
	free(dict);
	return upos;
}

unsigned char * pgx_decode(const struct pgx_desc *desc) {
	uint8_t *comp = malloc(desc->compressed_size);
	if (!comp) {
		return NULL;
	}

	const size_t dims = desc->width * desc->height * 4;
	uint8_t *out = malloc(dims);
	if (!out) {
		free(comp);
		return NULL;
	}

	fseek(desc->ifp, -(long)(desc->compressed_size), SEEK_END);
	const size_t read = fread(comp, 1, desc->compressed_size, desc->ifp);
	const size_t written = lzss_decomp(out, dims, comp, read);
	free(comp);
	if (!written) {
		free(out);
		puts("PGX Error: No data could be decoded.");
		return NULL;
	} else if (written < dims) {
		puts(RASTER_EOF);
	}
	return out;
}

enum lib_fail pgx_read_header(struct pgx_desc *desc) {
	/* PGX header (after signature):
		Offset  Size    Name
		0       BYTE    StartingBytes[4];
		4       DWORD   Width;
		8       DWORD   Height;
		12      WORD    IsTransparent;
		14      WORD    ???;
		16      DWORD   CompressedSize; // LZSS compressed
		20
	*/

	fseek(desc->ifp, 8, SEEK_SET);

	uint8_t buf[16];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return lib_unexpected_eof;
	}

	desc->width = buf_endian32(buf, little_endian);
	desc->height = buf_endian32(buf + 4, little_endian);
	desc->transparent = buf_endian16(buf + 8, little_endian);
	desc->compressed_size = buf_endian32(buf + 12, little_endian);
	if (!desc->width || !desc->height) {
		return lib_invalid_header;
	}
	return lib_ok;
}

enum lib_fail pgx_open_file(FILE *ifp, struct pgx_desc *desc) {
	const unsigned char sig[] = {'P', 'G', 'X', 0};
	unsigned char buf[sizeof(sig)];
	if (fread(buf, 1, sizeof(buf), ifp) == sizeof(buf)) {
		if (!memcmp(buf, sig, sizeof(buf))) {
			desc->ifp = ifp;
			return lib_ok;
		}
		return lib_invalid_signature;
	}
	return lib_unexpected_eof;
}
