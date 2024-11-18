// SPDX-License-Identifier: 0BSD
#include "misc/mem.h"
#include "raster/fmt.h"
#include "pgx.h"

static size_t lzss_decomp(uint8_t *restrict unpack, size_t ulen,
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
				const size_t dict_offset = (second & 0xf0u) << 4 | first;
				const size_t offset = 1
					+ ((upos - 19 - dict_offset) & dict_mask);
				memrepeat_or_zero(unpack, upos, offset, count);
				upos += count;
			}
		}
	}
	return upos;
}

size_t pgx_decode(const struct pgx_desc *desc, struct wuimg *img) {
	size_t written = 0;
	if (wuimg_alloc_noverify(img)) {
		const uint8_t *comp = mp_slice_at(&desc->mp,
			desc->mp.len - desc->comp_size, desc->comp_size);
		written = lzss_decomp(img->data, wuimg_size(img),
			comp, desc->comp_size);
	}
	return written;
}

enum wu_error pgx_read_header(struct pgx_desc *desc, struct wuimg *img) {
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

	const uint8_t *buf = mp_next_slice(&desc->mp, 20);
	if (!buf) {
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
	return desc->comp_size < 0xffffffff - 32 && desc->comp_size + 32 <= desc->mp.len
		? wuimg_verify(img)
		: wu_unexpected_eof;
}

enum wu_error pgx_init(struct pgx_desc *desc, const struct map_info map) {
	desc->mp = mp_map(map);
	const unsigned char sig[] = {'P', 'G', 'X', 0};
	return fmt_sigcmp_mem(sig, sizeof(sig), &desc->mp);
}
