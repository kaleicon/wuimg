// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <string.h>

#include "misc/common.h"
#include "raster/fmt.h"
#include "wgtspr.h"

void wgtspr_cleanup(struct wgtspr_desc *desc) {
	palette_unref(desc->pal);
}

struct wu_st wgtspr_get_sprite(const struct wgtspr_desc *desc, struct wuimg *img) {
	return fmt_load_raster_st(img, desc->ifp);
}

struct wu_st wgtspr_next_sprite(struct wgtspr_desc *desc, struct wuimg *img) {
	uint16_t buf[3];
	const size_t read = fread(buf, sizeof(*buf), ARRAY_LEN(buf), desc->ifp);
	if (!read) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint16_t used = endian16l(buf[0]);
	if (used) {
		if (read == ARRAY_LEN(buf)) {
			img->w = endian16l(buf[1]);
			img->h = endian16l(buf[2]);
			img->channels = 1;
			img->bitdepth = 8;
			img->bitrange = 6;
			wuimg_palette_set(img, palette_ref(desc->pal));
			return WU_OK;
		}
		return WUERR_HERE(wu_unexpected_eof);
	}
	fseek(desc->ifp, -4, SEEK_CUR);
	return WU_NO_CHANGE;
}

struct wu_st wgtspr_init(struct wgtspr_desc *desc, FILE *ifp) {
	*desc = (struct wgtspr_desc){
		.ifp = ifp,
	};
	const uint8_t magic[13] = " Sprite File ";
	uint8_t buf[2 + sizeof(magic)];
	if (!fread(buf, sizeof(buf), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	desc->version = buf_endian16l(buf);
	if (desc->version <= 5 && !memcmp(magic, buf + 2, sizeof(magic))) {
		struct palette *pal = palette_new();
		if (pal) {
			desc->pal = pal;
			if (palette_from_file(pal, 3, 256, ifp, 6)
			&& fread(buf, 2, 1, ifp)) {
				desc->sprites = buf_endian16l(buf)
					+ (uint32_t)(desc->version >= 4);
				return WU_OK;
			}
			return WUERR_HERE(wu_unexpected_eof);
		}
		return WUERR_HERE(wu_alloc_error);
	}
	return WUERR_HERE(wu_invalid_signature);
}
