#include "misc/endian.h"
#include "px.h"

static void dec_wrap(const struct px_desc *desc, struct wuimg *img,
const uint16_t *map) {
	uint32_t *data = (uint32_t *)img->data;
	const uint32_t *end = data + img->w * img->h;

	const struct px_tile *tile = &desc->tile;
	uint8_t wh[2];
	const size_t tilept = tile->len + 2 /* unknown data on both axis */;
	const long tile_bytes = (long)(sizeof(wh) + tilept * tilept * sizeof(*data));
	for (uint16_t y = 0; y < tile->h; ++y) {
		for (uint16_t x = 0; x < tile->w; ++x) {
			const int idx = tile->w * y + x;
			const long nb = endian16(map[idx], little_endian);
			if (!nb) {
				continue;
			}

			fseek(desc->ifp, tile->data_start + (nb-1) * tile_bytes, SEEK_SET);
			if (!fread(wh, sizeof(wh), 1, desc->ifp)
			|| wh[0] < 2 || wh[1] < 2) {
				continue;
			}

			uint32_t *base = data
				+ y * tile->len * img->w
				+ x * tile->len;
			for (uint8_t row = 0; row < wh[1] - 2; ++row) {
				uint32_t *d = base + img->w * row;
				if (d + wh[0] - 2 > end) {
					break;
				}
				fread(d, wh[0] - 2, sizeof(*d), desc->ifp);
				fseek(desc->ifp, 2 * sizeof(*d), SEEK_CUR);
			}
		}
	}
}

enum wu_error px_decode(const struct px_desc *desc, struct wuimg *img,
const uint32_t i) {
	img->w = desc->w;
	img->h = desc->h;
	img->channels = 4;
	img->bitdepth = 8;
	img->layout = pix_bgra;

	enum wu_error err = wu_decoding_error;
	uint16_t *map;
	const size_t map_len = desc->tile.w * desc->tile.h * sizeof(*map);
	map = malloc(map_len);
	if (map) {
		fseek(desc->ifp, 32 + (long)(i*map_len), SEEK_SET);
		if (fread(map, map_len, 1, desc->ifp)) {
			err = wuimg_verify(img);
			if (err == wu_ok) {
				img->data = calloc(1, wuimg_size(img));
				if (img->data) {
					dec_wrap(desc, img, map);
				} else {
					err = wu_alloc_error;
				}
			}
		} else {
			err = wu_unexpected_eof;
		}
		free(map);
	} else {
		err = wu_alloc_error;
	}
	return err;
}

enum wu_error px_parse(struct px_desc *desc, FILE *ifp) {
	/* PX header:
		Offset  Size    Name
		0       u32     ImageCount
		4       u32     TileSize
		8       u8      ???[8]
		16      u16     Type        // 0x0c
		18      u16     Bitdepth
		20      u16     Width
		22      u16     Height
		24      u8      ???[4]
		28      u16     TileWidth
		30      u16     TileHeight
		32
	*/
	uint16_t buf[16];
	if (!fread(buf, sizeof(buf), 1, ifp)) {
		return wu_unexpected_eof;
	}

	*desc = (struct px_desc) {
		.ifp = ifp,
		.nr = buf_endian32(buf, little_endian),
		.w = endian16(buf[10], little_endian),
		.h = endian16(buf[11], little_endian),
		.type = endian16(buf[8], little_endian),
		.tile.len = buf_endian32(buf + 2, little_endian),
		.tile.w = endian16(buf[14], little_endian),
		.tile.h = endian16(buf[15], little_endian),
	};
	const long tiles = desc->tile.w * desc->tile.h;
	desc->tile.data_start = (long)sizeof(buf) + desc->nr * (tiles * 2);

	if (desc->type != px_type_0c) {
		return wu_unknown_file_type;
	}

	if (!desc->nr || endian16(buf[9], little_endian) != 32) {
		return wu_invalid_header;
	}
	if (desc->tile.len * desc->tile.h > desc->h) {
		return wu_invalid_header;
	}
	return wu_ok;
}
