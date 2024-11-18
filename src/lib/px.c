// SPDX-License-Identifier: 0BSD
#include "misc/endian.h"
#include "px.h"

static void dec_wrap(const struct px_desc *desc, struct wuimg *img,
const uint16_t *map) {
	/* Tile struct:
		Offset  Type    Name
		0       u8      Width
		1       u8      Height
		2       u32     Pixels[(TileSize+2) * (TileSize+2)]

	 * Width and Height are the amount of meaningful data in the tile.
	 * However, two pixels on each axis are repeated from a neighbor tile
	 * (or from the last row or column if it's a border tile), so only
	 * data under (Width - 2) and (Height - 2) is used when compositing.
	*/
	uint32_t *data = (uint32_t *)img->data;
	const uint32_t *end = data + img->w * img->h;

	const struct px_tile *tile = &desc->tile;

	uint8_t wh[2];
	const size_t tsize = tile->size + 2;
	const long tile_bytes = (long)(sizeof(wh) + tsize * tsize * sizeof(*data));
	const long tile_off = tile->data_start - tile_bytes;
	for (uint16_t y = 0; y < tile->h; ++y) {
		for (uint16_t x = 0; x < tile->w; ++x) {
			const int idx = tile->w * y + x;
			const long nb = endian16(map[idx], little_endian);
			if (!nb) {
				continue;
			}

			fseek(desc->ifp, tile_off + nb * tile_bytes, SEEK_SET);
			if (!fread(wh, sizeof(wh), 1, desc->ifp) || wh[0] < 2) {
				continue;
			}

			uint32_t *base = data
				+ y * tile->size * img->w
				+ x * tile->size;
			for (uint8_t ty = 0; ty < wh[1] - 2; ++ty) {
				uint32_t *d = base + img->w * ty;
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
			err = wuimg_alloc(img);
			if (err == wu_ok) {
				dec_wrap(desc, img, map);
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
		4       u32     TileSize    // Width and Height of tiles
		8       u8      ???[8]
		16      u16     Type
		18      u16     Bitdepth
		20      u16     Width
		22      u16     Height
		24      u8      ???[4]
		28      u16     TilesX      // Tiles in X direction
		30      u16     TilesY      // Tiles in Y direction
		32      u16     TileOffsets[TilesX*TilesY]
	*/
	uint16_t buf[16];
	if (!fread(buf, sizeof(buf), 1, ifp)) {
		return wu_unexpected_eof;
	}

	*desc = (struct px_desc) {
		.ifp = ifp,
		.nr = buf_endian32(buf, little_endian),
		.type = endian16(buf[8], little_endian),
		.w = endian16(buf[10], little_endian),
		.h = endian16(buf[11], little_endian),
		.tile.size = buf_endian32(buf + 2, little_endian),
		.tile.w = endian16(buf[14], little_endian),
		.tile.h = endian16(buf[15], little_endian),
	};
	const long tiles = desc->tile.w * desc->tile.h;
	desc->tile.data_start = (long)sizeof(buf) + desc->nr * (tiles * 2);

	switch (desc->type) {
	case px_type_0c:
		if (endian16(buf[9], little_endian) != 32) {
			return wu_invalid_header;
		}
		return desc->nr ? wu_ok : wu_no_image_data;
	case px_type_01: case px_type_04: case px_type_07:
	case px_type_40: case px_type_44: case px_type_90:
		return wu_unsupported_feature;
	}
	return wu_invalid_header;
}
