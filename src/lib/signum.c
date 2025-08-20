// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
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
static void unpack_tile(uint16_t *dst, struct bitstrm *bs, struct mparser *mp,
const uint16_t htiles) {
	/* Tiles are 16x16 pixels. As each bit is a pixel, tiles are 2 bytes
	 * wide. */
	uint32_t bits = bitstrm_msb_peek_high25(bs);
	uint32_t mode = bits >> 30;
	bitstrm_msb_adv(bs, mode == 3 ? 2 : 6);
	if (mode == 3) {
		// Copy a tile from input
		for (size_t i = 0; i < 16; ++i) {
			memcpy(dst + i*htiles, mp->mem + mp->pos, sizeof(*dst));
			mp->pos += 2;
		}
	} else {
		// Split tile into 8x8 quadrants
		union {
			uint32_t cccc[8];
			uint16_t cc[16];
			uint8_t c[32];
		} buf;
		memset(&buf, 0, sizeof(buf));
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
		for (uint8_t i = 0; i < 16; ++i) {
			dst[i*htiles] = buf.cc[i];
		}
	}
}

static bool unpack_imc(const struct imc_desc *desc, uint16_t *data,
struct bitstrm *bs, struct mparser mp) {
	const size_t stride = desc->htiles * 16;
	uint8_t end[IMC_MAX_READ*2];
	for (uint32_t y = 0; y < desc->vtiles; ++y) {
		// If unset, skip this tile row
		if (bitstrm_msb_next(bs)) {
			for (uint32_t x = 0; x < desc->htiles; ++x) {
				// If unset, skip this tile
				if (bitstrm_msb_next(bs)) {
					if (mp.len - mp.pos < IMC_MAX_READ) {
						if (mp.mem == end) {
							return y | x;
						}
						mp.mem = mem_bufswitch(mp.mem,
							&mp.pos, &mp.len, end,
							sizeof(end));
					}
					unpack_tile(data + y*stride + x, bs,
						&mp, desc->htiles);
				}
			}
		}
	}
	return true;
}

size_t imc_decode(const struct imc_desc *desc, struct wuimg *img) {
	bool ok = false;
	struct mparser mp = desc->mp;
	const uint8_t *bits = mp_slice(&mp, desc->bitlen);
	if (bits) {
		const size_t elems = (size_t)16 * desc->vtiles * desc->htiles;
		uint16_t *dst = calloc(elems, sizeof(*dst));
		if (dst) {
			img->data = (uint8_t *)dst;
			struct bitstrm bs;
			bitstrm_from_bytes(&bs, bits, desc->bitlen);
			ok = unpack_imc(desc, dst, &bs, mp);
			/* XOR even rows with first xor byte, odd rows with
			 * second byte. */
			for (uint32_t y = 0; y < img->h; ++y) {
				uint8_t *row = (uint8_t *)(dst + y*desc->htiles);
				const uint8_t xor = desc->xor[(y & 1)];
				for (uint32_t x = 0; x < desc->htiles*2; ++x) {
					row[x] ^= xor;
				}
			}
		}
	}
	return ok;
}

static bool valid_dim(size_t d, size_t tiles) {
	return tiles - (d+15)/16 <= 1;
}

enum wu_error imc_parse(struct imc_desc *desc, struct wuimg *img) {
	/* IMC header (after signature):
		Offset  Type    Name
		0       u32     FileSize        // sans signature
		4       u16     Width
		6       u16     Height
		8       u16     HTiles
		10      u16     VTiles
		12      u32     BitstreamSize
		16      u32     BytestreamSize
		20      u16     XOR
		22      byte    ???[10]
		32
	*/
	const uint8_t *hdr = mp_slice(&desc->mp, 32);
	if (hdr) {
		img->w = buf_endian16(hdr + 4, big_endian);
		img->h = buf_endian16(hdr + 6, big_endian);
		img->channels = 1;
		img->bitdepth = 1;
		img->attr = pix_inverted;
		wuimg_align(img, 2);
		desc->htiles = buf_endian16(hdr + 8, big_endian);
		desc->vtiles = buf_endian16(hdr + 10, big_endian);
		desc->bitlen = buf_endian32(hdr + 12, big_endian);
		memcpy(&desc->xor, hdr + 20, sizeof(desc->xor));
		if (valid_dim(img->w, desc->htiles)
		&& valid_dim(img->h, desc->vtiles)) {
			return wuimg_verify(img);
		}
		return wu_invalid_header;
	}
	return wu_unexpected_eof;
}

enum wu_error imc_identify(struct imc_desc *desc, const struct wuptr mem) {
	*desc = (struct imc_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t sig[8] = "bimc0002";
	return fmt_sigcmp_mem(sig, sizeof(sig), &desc->mp);
}
