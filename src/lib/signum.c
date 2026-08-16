// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/mem.h"
#include "raster/fmt.h"

#include "lib/signum.h"

/* Docs:
https://github.com/Xiphoseer/sdo-tool/blob/main/docs/formats/bimc.md
*/

#define IMC_MAX_READ (9*4)
static void imc_unpack_tile(uint16_t *dst, struct bitstrm *bs,
struct mparser *mp, const uint16_t htiles, const size_t tile_height) {
	/* Tiles are 16x16 pixels. As each bit is a pixel, tiles are 2 bytes
	 * wide. */
	uint32_t bits = bitstrm_msb_peek_high25(bs);
	uint32_t mode = bits >> 30;
	bitstrm_msb_adv(bs, mode == 3 ? 2 : 6);
	if (mode == 3) {
		// Copy a tile from input
		for (size_t i = 0; i < tile_height; ++i) {
			memcpy(dst + i*htiles, mp->mem + mp->pos + i*2,
				sizeof(*dst));
		}
		mp->pos += 16*2;
	} else {
		// Split tile into 8x8 quadrants
		union {
			uint32_t cccc[8];
			uint16_t cc[16];
			uint8_t c[32];
		} buf = {0};
		for (uint8_t i = 0; i < 4; ++i) {
			if ((bits << i) & (0x1u << 29)) {
				uint8_t mask = mp->mem[mp->pos];
				++mp->pos;
				for (uint8_t b = 0; b < 8; ++b) {
					int p = (i >> 1)*16 + b*2 + (i & 1);
					if ((mask << b) & 0x80) {
						buf.c[p] = mp->mem[mp->pos];
						++mp->pos;
					}
				}
			}
		}

		switch (mode) {
		case 0: break;
		case 1:
			for (size_t i = 1; i < ARRAY_LEN(buf.cc); ++i) {
				buf.cc[i] ^= buf.cc[i-1];
			}
			break;
		case 2:
			for (size_t i = 1; i < ARRAY_LEN(buf.cccc); ++i) {
				buf.cccc[i] ^= buf.cccc[i-1];
			}
			break;
		}
		for (size_t i = 0; i < tile_height; ++i) {
			dst[i*htiles] = buf.cc[i];
		}
	}
}

static void xor_row(uint16_t *row, uint16_t xor, size_t len) {
	xor |= (uint16_t)(xor << 8);
	for (size_t x = 0; x < len; ++x) {
		row[x] ^= xor;
	}
}

static bool imc_unpack(const struct imc_desc *desc, uint16_t *data,
struct bitstrm *bs, struct mparser mp, const size_t h) {
	const size_t tile_band = desc->htiles * 16;
	uint8_t end[IMC_MAX_READ*2];
	bool data_avail = true;
	for (uint32_t ty = 0; ty < desc->vtiles; ++ty) {
		const size_t th = desc->vtiles - 1u == ty
			? h - ty*16 : 16;
		uint16_t *band = data + ty*tile_band;
		// If unset, skip this tile row
		if (data_avail && bitstrm_msb_next(bs)) {
			for (uint32_t tx = 0; tx < desc->htiles; ++tx) {
				// If unset, skip this tile
				if (bitstrm_msb_next(bs)) {
					if (mp.len - mp.pos < IMC_MAX_READ) {
						if (mp.mem == end) {
							data_avail = false;
							break;
						}
						mp.mem = mem_bufswitch(mp.mem,
							&mp.pos, &mp.len, end,
							sizeof(end));
					}
					imc_unpack_tile(band + tx, bs,
						&mp, desc->htiles, th);
				}
			}
		}
		/* XOR even rows with first xor byte, odd rows with
		 * second byte. */
		for (size_t y = 0; y < th; ++y) {
			xor_row(band + y*desc->htiles, desc->xor[y&1],
				desc->htiles);
		}
	}
	return data_avail;
}

struct wu_st imc_decode(const struct imc_desc *desc, struct wuimg *img) {
	struct mparser mp = desc->mp;
	const uint8_t *bits = mp_slice(&mp, desc->bitlen);
	size_t ok = 0;
	if (bits && mp.pos < mp.len) {
		++ok;
		uint16_t *dst = (uint16_t *)img->data;
		struct bitstrm bs;
		bitstrm_from_bytes(&bs, bits, desc->bitlen);
		ok += imc_unpack(desc, dst, &bs, mp, img->h);
	}
	return wuerr_partial(ok, 2);
}

static bool imc_valid_dim(size_t d, size_t tiles) {
	return (d + 15)/16 == tiles;
}

struct wu_st imc_parse(struct imc_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* IMC header:
		Offset  Type    Name
		0       u8      Magic[8]
		8       u32     FileSize        // sans signature
		12      u16     Width
		14      u16     Height
		16      u16     HTiles
		18      u16     VTiles
		20      u32     BitstreamSize
		24      u32     BytestreamSize
		28      u16     XOR
		30      byte    ???[10]
		40
	*/
	desc->mp = mp_wuptr(mem);
	const uint8_t *hdr = mp_slice(&desc->mp, 40);
	if (hdr) {
		const uint8_t sig[8] = {'b','i','m','c','0','0','0','2'};
		if (!memcmp(hdr, sig, sizeof(sig))) {
			img->w = buf_endian16b(hdr + 12);
			img->h = buf_endian16b(hdr + 14);
			img->channels = 1;
			img->bitdepth = 1;
			img->cs.invert = true;
			img->align_sh = 1;
			desc->htiles = buf_endian16b(hdr + 16);
			desc->vtiles = buf_endian16b(hdr + 18);
			desc->bitlen = buf_endian32b(hdr + 20);
			memcpy(&desc->xor, hdr + 28, sizeof(desc->xor));
			if (imc_valid_dim(img->w, desc->htiles)
			&& imc_valid_dim(img->h, desc->vtiles)) {
				return WU_OK;
			}
			return wuerr(wu_invalid_header,
				"image dimensions and tiles mismatch");
		}
		return WUERR_HERE(wu_invalid_signature);
	}
	return WUERR_HERE(wu_unexpected_eof);
}
