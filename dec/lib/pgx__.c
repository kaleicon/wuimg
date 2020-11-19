#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../../common.h"
#include "common/lib.h"
#include "pgx.h"

static uint8_t * lzss_decomp(uint8_t *restrict unpack,
const uint8_t *restrict unpack_limit, const uint8_t *restrict pack,
const uint8_t *restrict pack_limit) {
	/* Not to be confused with the GML_ARC LZSS algorithm, which requires
	 * negating the input beforehand. */
	uint8_t *restrict dict = calloc(0x1000, 1);
	if (!dict) {
		return 0;
	}

	const uint_fast16_t dict_mask = 0xfff;
	uint_fast16_t dict_pos = 0xfee;
	uint_fast16_t mask = 0x100;
	uint_fast8_t flags = 1;

	while (unpack < unpack_limit) {
		if (mask & 0x100) {
			if (pack >= pack_limit) {
				break;
			}
			flags = *pack;
			mask = 1;
			++pack;
		}

		if (flags & mask) {
			if (pack >= pack_limit) {
				break;
			}
			*unpack = *pack;
			dict[dict_pos] = *pack;
			dict_pos = (dict_pos + 1) & dict_mask;
			++pack;
			++unpack;
		} else {
			if (pack >= pack_limit - 1) {
				break;
			}
			const uint8_t first = *pack;
			const uint8_t second = *(pack + 1) ^ 0x0f;
			pack += 2;

			size_t dict_offset = (second & 0xf0U) << 4 | first;
			const size_t count = (second & 0x0f) + 3;
			const uint8_t *restrict limit = unpack + count;
			while (unpack < limit && unpack < unpack_limit) {
				const uint8_t byte = dict[dict_offset];
				dict[dict_pos] = byte;
				*unpack = byte;
				dict_offset = (dict_offset + 1) & dict_mask;
				dict_pos = (dict_pos + 1) & dict_mask;
				++unpack;
			}
		}
		mask <<= 1;
	}
	free(dict);
	return unpack;
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

	const uint8_t *end = lzss_decomp(out, out + dims, comp, comp + read);
	const size_t written = (size_t)(end - out);
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
