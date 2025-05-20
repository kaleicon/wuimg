// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "misc/math.h"
#include "raster/fmt.h"
#include "lib/hel.h"

/* Herahera Animation (へらへらアニメ)
https://discmaster.textfiles.com/browse/657/FM%20Towns%20Free%20Software%20Collection%2010.iso/t_os/tool/helplay
*/

static const uint16_t FRAME_SIZE = 160/8*120;

size_t hel_render_frame(const struct wuptr map, struct wuimg *img,
const uint32_t frame) {
	size_t w = 0;
	size_t pos = 12 + frame*FRAME_SIZE;
	if (pos < map.len) {
		w = zumin(FRAME_SIZE, map.len - pos);
		if (frame) {
			for (size_t i = 0; i < w; ++i) {
				img->data[i] ^= map.ptr[pos + i];
			}
		} else {
			memcpy(img->data, map.ptr + pos, w);
		}
	}
	return w;
}

enum wu_error hel_identify(const struct wuptr map, struct wuimg *img, unsigned fps) {
	/* HEL structure:
		Offset  Type    Name
		0       u8      ID[4]
		4       u32     Version?
		8       u32     Frames
		12      u8      Data[160/8 * 120 * Frames]

	 * After the first frame, each frame must be XORed with the
	 * previous one.
	 * There's no playback speed field, but helplay uses a default of
	 * 100 milliseconds (10 fps).
	*/
	const uint8_t sig[] = {
		'h', 'e', '1', 0,
		1, 0, 0, 0
	};
	struct mparser mp = mp_wuptr(map);
	const enum wu_error st = fmt_sigcmp_mem(sig, sizeof(sig), &mp);
	if (st == wu_ok) {
		const uint8_t *num = mp_slice(&mp, 4);
		if (num) {
			img->w = 160;
			img->h = 120;
			img->channels = 1;
			img->bitdepth = 1;
			const uint32_t nr = buf_endian32(num, little_endian);
			if (!wuimg_frames_init(img, nr)) {
				return wu_alloc_error;
			}
			for (size_t i = 0; i < nr; ++i) {
				const bool ok = wuimg_frame_set(img, i,
					0, 0, img->w, img->h,
					1, (fps ? fps : 12), i == 0);
				if (!ok) {
					return wu_invalid_params;
				}
			}
			return wuimg_verify(img);
		}
		return wu_unexpected_eof;
	}
	return st;
}
