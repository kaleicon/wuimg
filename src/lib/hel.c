// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "misc/math.h"
#include "raster/fmt.h"
#include "lib/hel.h"

/* Herahera Animation (へらへらアニメ)
https://discmaster.textfiles.com/browse/657/FM%20Towns%20Free%20Software%20Collection%2010.iso/t_os/tool/helplay
*/

static const size_t HEL_HEADER_SIZE = 12;
static const uint16_t HEL_FRAME_SIZE = 160/8*120;

struct wu_st hel_render_frame(const struct wuptr map, struct wuimg *img,
const uint32_t frame) {
	size_t w = 0;
	size_t pos = HEL_HEADER_SIZE + frame*HEL_FRAME_SIZE;
	if (pos < map.len) {
		w = zumin(HEL_FRAME_SIZE, map.len - pos);
		if (frame) {
			for (size_t i = 0; i < w; ++i) {
				img->data[i] ^= map.ptr[pos + i];
			}
		} else {
			memcpy(img->data, map.ptr + pos, w);
		}
	}
	return wuerr_partial(w, HEL_FRAME_SIZE);
}

struct wu_st hel_identify(const struct wuptr map, struct wuimg *img, unsigned fps) {
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
	if (map.len > HEL_HEADER_SIZE) {
		if (!memcmp(map.ptr, sig, sizeof(sig))) {
			img->w = 160;
			img->h = 120;
			img->channels = 1;
			img->bitdepth = 1;
			const uint32_t nr = buf_endian32(map.ptr + sizeof(sig),
				little_endian);
			const unsigned den = fps ? fps : 10;
			if (!wuimg_anim_init(img, nr, 1, (uint32_t)den)) {
				return WUERR_HERE(wu_alloc_error);
			}
			for (size_t i = 0; i < nr; ++i) {
				wuimg_anim_frame_set(img, i, i == 0);
			}
			return WU_OK;
		}
		return WUERR_HERE(wu_invalid_signature);;
	}
	return WUERR_HERE(wu_unexpected_eof);
}
