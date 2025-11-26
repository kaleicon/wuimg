// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "misc/mem.h"
#include "misc/mparser.h"
#include "lib/kyg.h"

/* Kyss' KYG format, used in an obscure FM-Towns slideshow presumably-joke program.
 * We're supporting it as a joke.
https://discmaster.textfiles.com/view/660/FREESOFT.BIN/t_os/fugaku/source/kygload.c

 * Note that the header definition in the source is misleading; due to C struct
 * packing rules, it has padding between some fields, so it's 132 bytes long,
 * not 128. Presumably part of the joke.
*/

struct wu_st kyg_decode(const struct kyg_desc *desc, struct wuimg *img) {
	uint16_t *dst = (uint16_t *)img->data;
	const size_t w = img->w;
	const size_t dst_len = w * img->h;
	const struct wuptr src = desc->data;

	size_t d = 0;
	size_t s = 0;
	uint8_t from_stream = 0; // Must be u8 for wrap-around.
	while (s < src.len) {
		uint16_t val;
		uint8_t run;
		if (from_stream) {
			goto read_pixel;
		}

		uint8_t c = src.ptr[s];
		++s;
		from_stream = (c >> 4);
		uint8_t from_prev = c & 0x0f;
		if (!from_prev) {
read_pixel:
			// Read a pixel from the stream

			--from_stream; /* If `from_stream` was refilled with a
			 * zero, this will wrap-around to 0xff and disable
			 * look-backs for a while. None of the program images
			 * trigger this, and it's presumably not meant to
			 * happen, but still, who knows what sort of images we
			 * may meet in the future. */
			if (src.len - s < 2) {
				break;
			}
			val = buf_endian16l(src.ptr + s);
			s += 2;
			run = (bool)(val & 0x8000);
			/* We don't clear the top bit as the bitfield
			 * unpacker doesn't care, neither does OpenGL, and
			 * least of all do we. */
			//val &= 0x7fff;
		} else {
			// Read a pixel from previous line plus an offset.
			size_t lookback = w + 4 - (from_prev & 0x07);
			val = (lookback > d) ? 0 : dst[d - lookback];
			run = (bool)(from_prev & 0x08);
		}
		if (!run) {
			if (s >= src.len) {
				break;
			}
			run = src.ptr[s];
			++s;
		}
		if (dst_len - d < run) {
			break;
		}
		memset16(dst + d, &val, run);
		d += run;
	}
	return wuerr_partial(d, dst_len);
}

struct wu_st kyg_parse(struct kyg_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* KYG structure (after magic line, all fields little-endian):
		Offset  Type    Name
		0       u8      Magic[20]
		20      u8      Comment[80]
		100     u8      EndOfMessage
		101     u8      Padding
		102     u16     Width
		104     u16     Height
		106     u16     X
		108     u16     Y
		110     u8      Padding[2]
		112     u32     DataLen
		116     u16     Type
		118     u16     Colors       // Number of possible colors
		120     u8      Reserved[10]
		130     u8      Padding[2]
		132
	*/
	*desc = (struct kyg_desc) {0};
	struct mparser mp = mp_wuptr(mem);

	const uint8_t sig[20] = "KYGformat ver.0.10\x0d\x0a";
	const uint8_t *hdr = mp_slice(&mp, sizeof(sig));
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, sig, sizeof(sig))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	desc->comment = wuptr_trim_end_space(mp_avail(&mp, 80));
	hdr = mp_slice(&mp, 32);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	img->w = buf_endian16l(hdr + 2);
	img->h = buf_endian16l(hdr + 4);
	img->channels = 1;
	img->bitdepth = 16;
	/* Pixel bit layout is XGRB (x ggggg rrrrr bbbbb), which translated
	 * into our convention for bitfields means BRGA.*/
	img->layout = pix_layout_pack(1, 2, 0, 3);
	img->alpha = alpha_ignore;
	desc->x = buf_endian16l(hdr + 6);
	desc->y = buf_endian16l(hdr + 8);

	const uint16_t type = buf_endian16l(hdr + 16);
	const uint16_t colors = buf_endian16l(hdr + 18);
	if (type == 1 && colors == (1 << 15)) {
		if (wuimg_bitfield_from_id(img, 0x555)) {
			desc->data = mp_avail(&mp, buf_endian32l(hdr + 12));
			return WU_OK;
		}
		return WUERR_HERE(wu_alloc_error);
	}
	return wuerr(wu_invalid_header, "image type != 1 or colors != 1 << 15");
}
