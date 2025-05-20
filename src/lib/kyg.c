// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "misc/mem.h"
#include "raster/fmt.h"
#include "lib/kyg.h"

/* Kyss' KYG format, used in an obscure FM-Towns slideshow presumably-joke program.
 * We're supporting it as a joke.
https://discmaster.textfiles.com/view/660/FREESOFT.BIN/t_os/fugaku/source/kygload.c

 * Note that the header definition in the source is misleading; due to C struct
 * packing rules, it has padding between some fields, so it's 132 bytes long,
 * not 128. Presumably part of the joke.
*/

static size_t rle_unpack(uint16_t *dst, const size_t dst_stride,
const size_t dst_len, const struct wuptr src) {
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
			val = buf_endian16(src.ptr + s, little_endian);
			s += 2;
			run = (bool)(val & 0x8000);
			/* We don't clear the top bit as the bitfield
			 * unpacker doesn't care, neither does OpenGL, and
			 * least of all do we. */
			//val &= 0x7fff;
		} else {
			// Read a pixel from previous line plus an offset.
			size_t lookback = dst_stride + 4 - (from_prev & 0x07);
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
	return d;
}

size_t kyg_decode(const struct kyg_desc *desc, struct wuimg *img) {
	size_t w = 0;
	if (wuimg_alloc_noverify(img)) {
		w = rle_unpack((uint16_t *)img->data, img->w, img->w * img->h,
			mp_avail_at(&desc->mp, desc->mp.pos, desc->len));
	}
	return w;
}

enum wu_error kyg_parse(struct kyg_desc *desc, struct wuimg *img) {
	/* KYG structure (after magic line, all fields little-endian):
		Offset  Type    Name
		0       u8      Comment[80]
		80      u8      EndOfMessage
		81      u8      Padding
		82      u16     Width
		84      u16     Height
		86      u16     X
		88      u16     Y
		90      u8      Padding[2]
		92      u32     DataLen
		96      u16     Type
		98      u16     Colors       // Number of possible colors
		100     u8      Reserved[10]
		110     u8      Padding[2]
		112
	*/
	desc->comment = wuptr_trim_end(mp_avail(&desc->mp, 80), ' ');
	mp_seek_cur(&desc->mp, 2);
	const uint8_t *hdr = mp_slice(&desc->mp, 30);
	if (!hdr) {
		return wu_unexpected_eof;
	}

	img->w = buf_endian16(hdr, little_endian);
	img->h = buf_endian16(hdr + 2, little_endian);
	img->channels = 1;
	img->bitdepth = 16;
	/* Pixel bit layout is XGRB (x ggggg rrrrr bbbbb), which translated
	 * into our convention for bitfields means BRGA.*/
	img->layout = pix_layout_pack(1, 2, 0, 3);
	img->alpha = alpha_ignore;
	desc->x = buf_endian16(hdr + 4, little_endian);
	desc->y = buf_endian16(hdr + 6, little_endian);
	desc->len = buf_endian32(hdr + 10, little_endian);

	const uint16_t type = buf_endian16(hdr + 14, little_endian);
	const uint16_t colors = buf_endian16(hdr + 16, little_endian);
	if (type == 1 && colors == (1 << 15)) {
		if (wuimg_bitfield_from_id(img, 0x555)) {
			return wuimg_verify(img);
		}
		return wu_alloc_error;
	}
	return wu_invalid_header;
}

enum wu_error kyg_identify(struct kyg_desc *desc, const struct wuptr map) {
	const uint8_t sig[20] = "KYGformat ver.0.10\x0d\x0a";
	*desc = (struct kyg_desc) {.mp = mp_wuptr(map)};
	return fmt_sigcmp_mem(sig, sizeof(sig), &desc->mp);
}
