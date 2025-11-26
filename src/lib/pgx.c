// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <string.h>

#include "misc/mem.h"
#include "raster/fmt.h"
#include "pgx.h"

static size_t pgx_decomp(uint8_t *restrict unpack, size_t ulen,
const uint8_t *restrict pack, size_t plen) {
	/* Not to be confused with the GML_ARC LZSS algorithm, which requires
	 * negating the input beforehand. */
	const uint16_t dict_mask = 0xfff;
	size_t upos = 0;
	size_t ppos = 0;

	uint8_t pend[(8*2 + 1) * 2];
	const size_t max_lzss_read = sizeof(pend)/2;
	for (;;) {
		if (ppos + max_lzss_read > plen) {
			if (pack == pend) {
				break;
			}
			pack = mem_bufswitch(pack, &ppos, &plen, pend,
				sizeof(pend));
		}

		uint8_t flags = pack[ppos];
		++ppos;
		for (size_t i = 0; i < 8; ++i, flags >>= 1) {
			if (flags & 1) {
				if (upos >= ulen) {
					return upos;
				}
				unpack[upos] = pack[ppos];
				++ppos;
				++upos;
			} else {
				const uint8_t first = pack[ppos];
				const uint8_t second = pack[ppos + 1];
				ppos += 2;

				const size_t count = 18 - (second & 0x0f);
				if (upos + count > ulen) {
					return upos;
				}
				const size_t dict_offset =
					(second & 0xf0u) << 4 | first;
				const size_t offset = 1
					+ ((upos - 19 - dict_offset) & dict_mask);
				memrepeat_or_zero(unpack, upos, offset, count);
				upos += count;
			}
		}
	}
	return upos;
}

struct wu_st pgx_decode(const struct wuptr src, struct wuimg *img) {
	const size_t ulen = wuimg_size(img);
	return wuerr_partial(pgx_decomp(img->data, ulen, src.ptr, src.len),
		ulen);
}

struct wu_st pgx_read_header(struct wuptr *comp, const struct wuptr mem,
struct wuimg *img) {
	/* PGX header (after signature):
		Offset  Size    Name
		0       BYTE[4] Magic;
		4       BYTE[4] StartingBytes; // of compressed data
		8       DWORD   Width;
		12      DWORD   Height;
		16      WORD    HasTransparency;
		18      BYTE    ???;
		19      BYTE    ExtraData?;
		20      DWORD   CompressedSize;
		24      BYTE[8] Padding?;
		32

	 * If ExtraData is set, there's some encrypted metadata of unknown size
	 * between the header and the compressed stream. Seek -CompressedSize
	 * bytes from the end of the file to skip it.
	*/

	const uint8_t sig[] = {'P', 'G', 'X', 0};
	if (mem.len <= 32) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(mem.ptr, sig, sizeof(sig))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	img->w = buf_endian32l(mem.ptr + 8);
	img->h = buf_endian32l(mem.ptr + 12);
	img->channels = 4;
	img->bitdepth = 8;
	img->layout = pix_bgra;
	img->alpha = buf_endian16l(mem.ptr + 16)
		? alpha_unassociated : alpha_ignore;

	const uint32_t comp_size = buf_endian32l(mem.ptr + 20);
	if (comp_size <= mem.len - 32) {
		const size_t pos = mem.len - comp_size;
		*comp = mem;
		comp->ptr += pos;
		comp->len -= pos;
		return WU_OK;
	}
	return WUERR_HERE(wu_unexpected_eof);
}
