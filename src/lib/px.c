// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "misc/endian.h"
#include "misc/math.h"
#include "px.h"

static uint32_t dec_wrap(const struct px_desc *desc, struct wuimg *img,
const uint8_t *map) {
	/* Tile struct:
		Offset  Type    Name
		0       u8      Width
		1       u8      Height
		2       u32     Pixels[Width * Height]

	 * Width and Height are the amount of data in the tile.
	 * However, two pixels on each axis are repeated from a neighbor tile
	 * (or from the last row or column if it's a border tile), so only
	 * data under (Width - 2) and (Height - 2) is used when compositing.
	*/
	uint32_t *data = (uint32_t *)img->data;
	const struct px_tile *tile = &desc->tile;

	const size_t tsize = tile->size + 2;
	const size_t tile_bytes = 2 + tsize * tsize * sizeof(*data);
	const size_t tile_off = tile->data_start - tile_bytes;
	uint32_t r = 0;
	for (uint16_t ty = 0; ty < tile->y; ++ty) {
		for (uint16_t tx = 0; tx < tile->x; ++tx) {
			const size_t y = ty*tile->size;
			const size_t x = tx*tile->size;
			if (y >= img->h || x >= img->w) {
				continue;
			}
			const size_t idx = (size_t)tile->x * ty + tx;
			const uint16_t nb = buf_endian16l(map + idx*sizeof(nb));
			if (!nb) {
				++r;
				continue;
			}

			const size_t off = tile_off + nb*tile_bytes;
			const uint8_t *wh = mp_slice_at(&desc->mp, off, 2);
			if (!wh) {
				continue;
			}

			const size_t tw = wh[0];
			const size_t th = wh[1];
			struct wuptr src = mp_avail_at(&desc->mp, off+2, tw*th);

			if (tw <= 2 || th <= 2) {
				continue;
			}
			const size_t max_w = zumin(tw - 2, img->w - x);
			const size_t max_h = zumin(zumin(th - 2, img->h - y),
				src.len / tw);
			uint32_t *row = data + y*img->w + x;
			for (uint8_t ry = 0; ry < max_h; ++ry) {
				memcpy(row + ry*img->w,
					src.ptr + ry * tw * sizeof(*row),
					max_w * sizeof(*row));
			}
			r += max_h > 0;
		}
	}
	return r;
}

struct wu_st px_get_image(const struct px_desc *desc, struct wuimg *img,
const uint32_t i) {
	const size_t map_len = desc->tile.x * desc->tile.y * sizeof(uint16_t);
	const uint8_t *map = mp_slice_at(&desc->mp, 32 + i*map_len, map_len);
	if (map) {
		return wuerr_partial(dec_wrap(desc, img, map),
			(size_t)desc->tile.x * desc->tile.y);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

struct wu_st px_set_info(const struct px_desc *desc, struct wuimg *img) {
	img->w = desc->w;
	img->h = desc->h;
	img->channels = 4;
	img->bitdepth = 8;
	img->layout = pix_bgra;
	return WU_OK;
}

struct wu_st px_parse(struct px_desc *desc, struct wuptr mem) {
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
		32      u16     TileOffsets[ImageCount][TilesX*TilesY]
	*/
	*desc = (struct px_desc) {
		.mp = mp_wuptr(mem),
	};
	const size_t header_size = 32;
	const uint8_t *buf = mp_slice(&desc->mp, header_size);
	if (!buf) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	desc->nr = buf_endian32l(buf);
	desc->type = buf_endian16l(buf + 16);
	desc->w = buf_endian16l(buf + 20);
	desc->h = buf_endian16l(buf + 22);
	desc->tile.size = buf_endian32l(buf + 4);
	desc->tile.x = buf_endian16l(buf + 28);
	desc->tile.y = buf_endian16l(buf + 30);
	const size_t tiles = desc->tile.x * desc->tile.y;
	desc->tile.data_start = header_size + desc->nr * tiles * sizeof(uint16_t);
	switch (desc->type) {
	case px_type_0c:
		if (buf_endian16l(buf + 18) != 32) {
			return wuerr(wu_invalid_header, "bitdepth != 32");
		}
		return WU_OK;
	case px_type_01: case px_type_04: case px_type_07:
	case px_type_40: case px_type_44: case px_type_90:
		break;
	}
	return wuerr(wu_unsupported_feature, "image type != 0x0c");
}
