// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "misc/decomp.h"
#include "misc/iff.h"
#include "misc/math.h"

#include "lib/siff.h"

/* BeamSoftware SIFF image/animation.
 * Like normal IFF, chunk lengths are big-endian despite contents being
 * little-endian.
 * Unlike normal IFF, chunks aren't aligned to word units. */

struct wu_st pim_decode(struct pim_desc *desc, struct wuimg *img) {
	struct wuptr src;
	if (desc->type == pim_anim) {
		const uint32_t len = buf_endian32l(desc->frame_sizes);
		src = mp_avail(&desc->mp, len);
	} else {
		src = mp_remaining(&desc->mp);
	}
	const size_t dst_len = wuimg_size(img);
	return wuerr_partial(
		decomp_topbitrle(img->data, dst_len, src.ptr, src.len, 1),
		dst_len);
}

static struct wu_st pim_body(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	(void)iff;
	struct pim_desc *desc = ptr;
	desc->start = desc->mp.pos;

	const char *msg = NULL;
	if (desc->type == pim_anim) {
		/* BODY contents for animations:
			Offset  Type    Name
			0       u8      ID[2]            // "AT"
			2       u32     FrameLen[Frames]
			--      u32     ???
			--      u8      RLEStream
		*/
		const struct wuptr data = mp_avail(&desc->mp, chunk.len);
		const size_t sizes = desc->frames*4u + 6;
		if (data.len < sizes) {
			return WUERR_HERE(wu_unexpected_eof);
		} else if (memcmp(data.ptr, "AT", 2)) {
			return wuerr(wu_invalid_header,
				"unknown stream start for animation");
		}
		desc->frame_sizes = data.ptr + 2;
		desc->start += sizes;
		desc->mp.pos = desc->start;
		msg = "animated PIM not supported, will only render first frame";
	}
	return wuerr(wu_ok, msg);
}

static struct wu_st pim_cmap(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	/* CMAP contents:
		Offset  Type    Name
		0       u8      RGB[Len/3][3]
	*/
	if (chunk.len > 0x300 || chunk.len % 3) {
		return wuerr(wu_invalid_header, "bad CMAP length");
	}
	struct pim_desc *desc = ptr;
	const uint8_t *hdr = mp_slice(&desc->mp, chunk.len);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct wuimg *img = desc->img;
	struct palette *pal = wuimg_palette_init(img);
	if (!pal) {
		return WUERR_HERE(wu_alloc_error);
	}

	palette_from_rgb8(pal, hdr, chunk.len/3);
	++iff->table;
	return iff_next_mparser(iff, &desc->mp, chunk);
}

static struct wu_st pim_ahdr(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	/* AHDR contents:
		Offset  Type    Name
		0       u8      ???         // Always 1?
		1       u8      ImageType   // 1 for static, 3 for animation
		2       u16     Width
		4       u16     Height
		6       u16     Frames      // [*]
		8       u16     PalEntries
		10      u16     ???         // Always 0?
		12      u16     ???
		14      u16     ???         // Always 0?
		16      u32     ???
		20      u8      ???[6]      // Always 0?
		26      u16     ???         // Sometimes negative? (X offset?)
		28      u16     ???         // Sometimes negative? (Y offset?)
		30      u16     ???         // Always 0?
		32
	 * [*] If not zero, compressed stream is preceded by frame sizes, and
	 *     there's a HOLD tag after BODY.
	*/
	if (chunk.len != 0x20) {
		return wuerr(wu_invalid_header, "AHDR with length != 0x20");
	}
	struct pim_desc *desc = ptr;
	const uint8_t *hdr = mp_slice(&desc->mp, chunk.len);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct wuimg *img = desc->img;
	switch (hdr[1]) {
	case pim_static:
	case pim_anim:
		break;
	default:
		return wuerr(wu_samples_wanted, "image type neither 1 or 3");
	}
	desc->type = hdr[1];
	img->w = buf_endian16l(hdr + 2);
	img->h = buf_endian16l(hdr + 4);
	img->channels = 1;
	img->bitdepth = 8;
	desc->frames = buf_endian16l(hdr + 6);

	++iff->table;
	return iff_next_mparser(iff, &desc->mp, chunk);
}

static const struct iff_table PIM_TABLE[] = {
	{FOURCC('A', 'H', 'D', 'R'), pim_ahdr},
	{FOURCC('C', 'M', 'A', 'P'), pim_cmap},
	{FOURCC('B', 'O', 'D', 'Y'), pim_body},
};

struct wu_st pim_parse(struct pim_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	*desc = (struct pim_desc) {.mp = mp_wuptr(mem), .img = img};
	const uint8_t *hdr = mp_slice(&desc->mp, 12);
	if (hdr) {
		if (!memcmp(hdr, "SIFF", 4) && !memcmp(hdr+8, "PXAN", 4)) {
			const uint32_t len = buf_endian32b(hdr + 4);
			desc->mp.len = zumin(desc->mp.len - desc->mp.pos, len);
			desc->mp.mem += desc->mp.pos;
			desc->mp.pos = 0;
			struct iff_state iff = {
				.table = PIM_TABLE,
				.table_len = 1,
				.endian = big_endian,
				.user = desc,
				.align_sh = 0,
			};
			return iff_next_mparser(&iff, &desc->mp,
				(struct iff_chunk){0});
		}
		return WUERR_HERE(wu_unknown_file_type);
	}
	return WUERR_HERE(wu_unexpected_eof);
}
