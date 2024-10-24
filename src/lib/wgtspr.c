#include "misc/mem.h"
#include "raster/fmt.h"
#include "wgtspr.h"

size_t wgtspr_get_sprite(const struct wgtspr_desc *desc, struct wuimg *img) {
	return wuimg_alloc_noverify(img) ? fmt_load_raster(img, desc->ifp) : 0;
}

enum wu_error wgtspr_next_sprite(struct wgtspr_desc *desc, struct wuimg *img) {
	uint16_t buf[3];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	const uint16_t used = endian16(buf[0], little_endian);
	if (used) {
		img->w = endian16(buf[1], little_endian);
		img->h = endian16(buf[2], little_endian);
		img->channels = 1;
		img->bitdepth = 8;
		if (wuimg_palette_set(img, memdup(&desc->pal, sizeof(desc->pal)))) {
			return wuimg_verify(img);
		}
		return wu_alloc_error;
	}
	fseek(desc->ifp, -4, SEEK_CUR);
	return wu_no_change;
}

enum wu_error wgtspr_init(struct wgtspr_desc *desc, FILE *ifp) {
	const uint8_t magic[13] = " Sprite File ";
	uint8_t buf[2 + sizeof(magic)];
	if (!fread(buf, sizeof(buf), 1, ifp)) {
		return wu_unexpected_eof;
	}

	desc->version = buf_endian16(buf, little_endian);
	if (desc->version <= 5 && !memcmp(magic, buf + 2, sizeof(magic))) {
		struct raster_pal *pal = &desc->pal;
		const enum wu_error st = fmt_load_pal(ifp, pal, fmt_pal_rgb, 256);
		if (st == wu_ok) {
			if (fread(buf, 2, 1, ifp)) {
				desc->sprites = buf_endian16(buf, little_endian)
					+ (uint32_t)(desc->version >= 4);
				desc->ifp = ifp;
				const uint8_t sh = 8;
				const unsigned scale = (0xff << sh) / 0x3f + 1;
				for (size_t i = 0; i < ARRAY_LEN(pal->color); ++i) {
					struct pix_rgba8 *c = pal->color + i;
					c->r = (uint8_t)((c->r * scale) >> 8);
					c->g = (uint8_t)((c->g * scale) >> 8);
					c->b = (uint8_t)((c->b * scale) >> 8);
				}
				return wu_ok;
			}
			return wu_unexpected_eof;
		}
		return st;
	}
	return wu_unknown_file_type;
}
