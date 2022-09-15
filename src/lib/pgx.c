#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "common/file.h"
#include "common/mem.h"
#include "raster/fmt.h"
#include "pgx.h"

static const size_t LZSS_PAD = 2 * 8 + 1;

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
				memrepeat_or_zero(unpack, upos, offset, count);
				upos += count;
			}
		}
	}
	return upos;
}

size_t pgx_decode(const struct pgx_desc *desc, struct raw_img *img) {
	size_t written = 0;
	if (raw_img_alloc_noverify(img)) {
		uint8_t *comp = malloc(desc->comp_size + LZSS_PAD);
		if (comp) {
			const size_t read = file_tail(comp, 1, desc->comp_size,
				desc->ifp);
			written = lzss_decomp(img->data, raw_img_size(img),
				comp, read);
			free(comp);
		}
	}
	return written;
}

enum wu_error pgx_read_header(struct pgx_desc *desc, struct raw_img *img) {
	/* PGX header (after signature):
		Offset  Size    Name
		0       BYTE[4] StartingBytes; // of compressed data
		4       DWORD   Width;
		8       DWORD   Height;
		12      WORD    HasTransparency;
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

	uint8_t buf[20];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	img->w = buf_endian32(buf + 4, little_endian);
	img->h = buf_endian32(buf + 8, little_endian);
	img->channels = 4;
	img->bitdepth = 8;
	img->layout = pix_bgra;
	img->alpha = buf_endian16(buf + 12, little_endian)
		? alpha_unassociated : alpha_ignore;
	desc->comp_size = buf_endian32(buf + 16, little_endian);
	return raw_img_verify(img);
}

enum wu_error pgx_open_file(struct pgx_desc *desc, FILE *ifp) {
	desc->ifp = ifp;
	const unsigned char sig[] = {'P', 'G', 'X', 0};
	return fmt_sigcmp(sig, sizeof(sig), ifp);
}
