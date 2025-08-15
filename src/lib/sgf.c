// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "misc/endian.h"
#include "misc/mem.h"

#include "lib/sgf.h"

/* Somera Graphics Format, used in the "Touch Games" series */

static const size_t HEADER_SIZE = 0x100;

struct wu_st sgf_decode(struct sgf_desc *desc, struct wuimg *img) {
	uint16_t *dst = (uint16_t *)img->data;
	const size_t dst_len = img->w*img->h;
	size_t d = 0;
	while (desc->mp.len - desc->mp.pos >= 4) {
		const uint16_t w = buf_endian16(desc->mp.mem + desc->mp.pos,
			little_endian);
		const uint16_t cnt = w & 0x7fff;
		if (dst_len - d < cnt) {
			break;
		}
		desc->mp.pos += 2;
		if (w & 0x8000) {
			const uint16_t val = buf_endian16(
				desc->mp.mem + desc->mp.pos,
				little_endian);
			memset16(dst + d, &val, cnt);
			desc->mp.pos += 2;
		} else {
			if (desc->mp.len - desc->mp.pos < cnt*2) {
				break;
			}
			memcpy(dst + d, desc->mp.mem + desc->mp.pos, cnt*2);
			desc->mp.pos += cnt*2;
		}
		d += cnt;
	}
	desc->idx += 1;
	return wuerr_partial(d, dst_len);
}

struct wu_st sgf_setup_next(struct sgf_desc *desc, struct wuimg *img) {
	/* SGF image struct:
		Offset  Type    Name
		0       u16     Width
		2       u16     Height
		4       u16     RLEStream[] // Read until buffer is full
	*/
	const uint8_t *dims = mp_slice(&desc->mp, 4);
	if (dims) {
		img->w = buf_endian16(dims, little_endian);
		img->h = buf_endian16(dims + 2, little_endian);
		img->channels = 1;
		img->bitdepth = 16;
		img->layout = pix_bgra;
		return wuimg_bitfield_from_id(img, 0x565)
			? WU_OK : WUERR_HERE(wu_alloc_error);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

struct wu_st sgf_parse(struct sgf_desc *desc, const struct wuptr mem) {
	/* SGF header:
		Offset  Type    Name
		0       u8      NrImages
		1       char    ID[0x35]
		0x36    u8      Padding[0xca] // Always zeros
		0x100
	*/
	const uint8_t magic[0x35] =
		"SoMERA GRaPHIc "
		"FORMAT r10 - by "
		"T.Pomar a.k.a. S"
		"obakus";
	*desc = (struct sgf_desc){.mp = mp_wuptr(mem)};
	const uint8_t *hdr = mp_slice(&desc->mp, HEADER_SIZE);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr + 1, magic, sizeof(magic))
	|| memchk(hdr + 1 + sizeof(magic), 0, HEADER_SIZE - sizeof(magic) - 1)) {
		return WUERR_HERE(wu_invalid_header);
	}
	desc->nr = hdr[0] + 1;
	return WU_OK;
}
