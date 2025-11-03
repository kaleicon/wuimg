// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include "misc/mem.h"
#include "raster/fmt.h"
#include "lib/gpc.h"

/* Decoding algorithm from
https://github.com/HolografixFinn/gpc2bmp
 * plus original research.

 * TODO: What's wrong with Himitu_c/pic/s53.gpc (from 秘密の花園)?
*/

void gpc_cleanup(struct gpc_desc *desc) {
	palette_unref(desc->pal);
}

static uint32_t bitspread(uint32_t c) {
	/* Spreads a byte's bits so that they're 4 bits apart. */
	c *= 0x40100401; // replicate every 10 bits
	c &= 0xc0c0c0c0; // isolate bit pairs
	c |= c >> 3;     // replicate bit pairs at an offset
	c &= 0x88888888; // isolate bits in {1,0, 3,2, 5,4, 7,6} order
	return c;        // on Little-Endian, we're done. on BE, swap later
}

static void gpc_decorrelate(struct wuimg *img, uint8_t *restrict src,
const size_t plane_len, const size_t row_size, const size_t row_skip) {
	uint8_t *dst = img->data;
	const size_t stride = wuimg_stride(img);
	size_t cycle = 0;
	size_t dst_y = cycle;
	for (size_t y = 0; y < img->h; ++y) {
		/* Rows are split into 4 bitplanes, plus an extra byte at the
		 * start. */
		const size_t row_off = row_size*y;
		const uint8_t xskip = src[row_off];
		if (xskip) {
			/* XOR horizontally, with `xskip` bytes of stride. On
			 * reaching the end, wrap around and do the same to the
			 * bytes in between. `xor` is reused when wrapping. */
			uint8_t xor = 0;
			for (size_t i = 0; i < xskip; ++i) {
				for (size_t x = i+1; x < row_size; x += xskip) {
					xor ^= src[row_off + x];
					src[row_off + x] = xor;
				}
			}
		}
		if (y) {
			/* XOR with the previous row. */
			for (size_t x = 1; x < row_size; ++x) {
				src[row_off + x] ^= src[row_off + x - row_size];
			}
		}

		/* Merge bitplanes into 4-bit quantities, lower planes into
		 * least-significant positions. and write to every `row_skip`
		 * row of the output buffer. On reaching the end, wrap around
		 * to fill the next row group. */
		uint8_t *l1 = src + row_off + 1;
		uint8_t *l2 = l1 + plane_len;
		uint8_t *l3 = l2 + plane_len;
		uint8_t *l4 = l3 + plane_len;
		size_t d = dst_y * stride;
		for (size_t i = 0; i < plane_len; ++i) {
			/* We can handle 4-bit data, so pack 8 pixels into
			 * u32 words. */
			const uint32_t a = bitspread(l1[i]) >> 3
				| bitspread(l2[i]) >> 2
				| bitspread(l3[i]) >> 1
				| bitspread(l4[i]);
			((uint32_t *)(dst + d))[i] = endian32l(a);
		}

		dst_y += row_skip;
		if (dst_y >= img->h) {
			++cycle;
			dst_y = cycle;
		}
	}
}

#define MAX_READ (1 + (8 * (1 + 8)))
static size_t gpc_decomp(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, size_t src_len) {
	size_t d = 0;
	size_t s = 0;
	uint8_t end[MAX_READ*2];
	while (d < dst_len) {
		if (s + MAX_READ > src_len) {
			if (src == end) {
				break;
			}
			src = mem_bufswitch(src, &s, &src_len, end, sizeof(end));
		}

		uint8_t iflags = src[s];
		++s;
		for (size_t i = 0; i < 8; ++i, iflags <<= 1) {
			const size_t pos = d*64 + i*8;
			if (iflags & 0x80) {
				uint8_t kflags = src[s];
				++s;
				for (size_t k = 0; k < 8; ++k, kflags <<= 1) {
					if (kflags & 0x80) {
						dst[pos + k] = src[s];
						++s;
					} else {
						dst[pos + k] = 0;
					}
				}
			} else {
				memset(dst + pos, 0, 8);
			}
		}
		++d;
	}
	return d;
}

struct wu_st gpc_decode(const struct gpc_desc *desc, struct wuimg *img) {
	size_t w = 0;
	const size_t plane_len = strip_base(img->w, 1);
	const size_t row_size = plane_len * 4 + 1;
	/* Request a buffer padded to 64 bytes, as the decompressor
	 * writes that much data per iteration. */
	const size_t tmp_size = strip_length(row_size * img->h, 8,
		align_from_int(64));
	uint8_t *tmp = calloc(tmp_size, 1);
	if (tmp) {
		const struct wuptr comp = mp_avail_at(&desc->mp,
			desc->mp.pos, desc->cur.comp_len);
		w = gpc_decomp(tmp, tmp_size/64, comp.ptr, comp.len);
		gpc_decorrelate(img, tmp, plane_len, row_size,
			desc->cur.row_skip);
		free(tmp);
	}
	return wuerr_partial(w, tmp_size/64);
}

static uint32_t get_offset(struct gpc_desc *desc, const size_t i) {
	return buf_endian32l(desc->sub_info + (i+1)*4);
}

struct wu_st gpc_set_image(struct gpc_desc *desc, struct wuimg *img,
const uint32_t i) {
	if (i) {
		const uint32_t off = get_offset(desc, i);
		const uint32_t next = get_offset(desc, i+1);
		if (next < 10 || next - 10 <= off) {
			return wuerr(wu_invalid_header, "too small image header");
		}

		mp_seek_set(&desc->mp, (size_t)desc->sub_off + off);
		const uint8_t *hdr = mp_slice(&desc->mp, 10);
		if (!hdr) {
			return WUERR_HERE(wu_unexpected_eof);
		}

		desc->cur = (struct gpc_img_settings) {
			.row_skip = buf_endian16l(hdr),
			.comp_len = next - 10 - off,
			.x = buf_endian16l(hdr + 2),
			.y = buf_endian16l(hdr + 4),
		};
		img->w = buf_endian16l(hdr + 6);
		img->h = buf_endian16l(hdr + 8);
	} else {
		mp_seek_set(&desc->mp, desc->img_off);
		const uint8_t *hdr = mp_slice(&desc->mp, 16);
		if (!hdr) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		img->w = buf_endian16l(hdr);
		img->h = buf_endian16l(hdr + 2);
		desc->cur = (struct gpc_img_settings) {
			.row_skip = desc->main_row_skip,
			.comp_len = buf_endian32l(hdr + 4),
			.x = buf_endian16l(hdr + 10),
			.y = buf_endian16l(hdr + 12),
		};
	}
	img->channels = 1;
	img->bitdepth = 4;
	img->bitrange = 4;
	img->layout = pix_grba;
	wuimg_align(img, 4);
	wuimg_palette_set(img, palette_ref(desc->pal));
	return WU_OK;
}

static void read_sub_data(struct gpc_desc *desc) {
	mp_seek_set(&desc->mp, desc->sub_off);
	const uint8_t *sub_header = mp_slice(&desc->mp, 4);
	if (sub_header) {
		const uint32_t nb = buf_endian32l(sub_header);
		if (nb < UINT32_MAX - 3) {
			desc->sub_info = mp_slice(&desc->mp, (nb+3)*4);
			if (desc->sub_info) {
				desc->nb = nb + 1;
			}
		}
	}
}

static struct wu_st load_pal(struct gpc_desc *desc) {
	const uint8_t *header = mp_slice(&desc->mp, 4);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint16_t nb = buf_endian16l(header);
	const uint16_t elem_size = buf_endian16l(header+2);
	if (elem_size != 2 || nb > 16) {
		return wuerr(wu_invalid_header, "bad palette dimensions");
	}

	const uint16_t total = nb*elem_size;
	const uint8_t *data = mp_slice(&desc->mp, total);
	if (!data) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct palette *pal = palette_new();
	if (!pal) {
		return WUERR_HERE(wu_alloc_error);
	}
	desc->pal = pal;

	for (size_t i = 0; i < nb; ++i) {
		const uint16_t c = buf_endian16l(data + i*2);
		pal->color[i] = (struct pix_rgba8) {
			.r = (c >> 8) & 0xf,
			.g = (c >> 4) & 0xf,
			.b = c & 0xf,
			.a = 0xf,
		};
	}
	return WU_OK;
}

struct wu_st gpc_parse(struct gpc_desc *desc, const struct wuptr mem) {
	/* GPC header, little-endian:
		Offset  Type    Name
		0       u8      Signature[16]
		0       u32     RowSkip       // May be 0 for Height=1 images
		4       u32     PaletteOffset
		8       u32     ImageOffset
		12      u32     SubOffset     // Used only in "ナイキ"? [1]
		16      u32     FileSize      // Often 0
		20      u8      ???[10]       // Always 0?
		30      u16     EnableTrns??? // 0, 1, 0x0C00 in "MAP0.GPC"
		32

	 * Palette:
		0       u16     Nb            // Always 16?
		2       u16     ESize         // Always 2?
		4       ESize   Entries[Nb]
		...     u8      Maker[]?      // [2]

	 * Image:
		0       u16     Width
		2       u16     Height
		4       u32     CompressedLen
		8       u16     Bitdepth?     // Always 4?
		10      u16     X
		12      u16     Y
		14      u16     TrnsIdx???    // "ナイキ"-only
		16      u8      Data[]

	 * SubRegionIndex (when SubOffset is not zero):
		0       u32     Nb
		4       u32     Size                // yes, Size appears thrice
		8       u32     Size
		12      u32     SubRegionOffset[Nb]
		...     u32     Size                // dummy Offset i guess

	 * SubRegion struct (same palette as main image):
		0       u16     RowSkip?
		2       u16     X
		4       u16     Y
		6       u16     Width
		8       u16     Height
		10      u8      Data[]   // Spans up to the next SubRegion

	 * Even deeper Mystery struct (after SubRegionIndex):
		0       u16     Nb
		2       u16     Size
		4       u16     ???          // Always 0?
		6       u16     ???          // Always 0?
		8       struct  Marianna[Nb]
		...     struct  Marianna     // dummy struct whose DataOffset == Size
		...     u8      Data[]       // no clue

	 * Marianna's struct:
		0       u16     DataOffset  // Relative to Mystery struct
		2       u16     ???         // Always 0?
		4       u16     ???         // Always 0?
		6       u16     ???         // Always 0?
		8

	 * [1] Which also seems to be the first release using this format.
	 * [2] Sometimes the string "n*bys\0" is present, which is the
	 *     programmer alias as per the credits screen images.
	 *     It doesn't seem correlated to any field, so the only way to
	 *     detect it is by checking for a gap between the palette end
	 *     and the image.
	 */

	*desc = (struct gpc_desc) {
		.mp = mp_wuptr(mem),
	};
	const unsigned char sig[16] = {
		'P', 'C', '9', '8',
		')', 'G', 'P', 'C',
		'F', 'I', 'L', 'E',
		' ', ' ', ' ', 0
	};
	const unsigned char *header = mp_slice(&desc->mp, 48);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(header, sig, sizeof(sig))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	desc->main_row_skip = buf_endian32l(header + 16);
	const uint32_t pal_off = buf_endian32l(header + 20);
	desc->img_off = buf_endian32l(header + 24);
	desc->sub_off = buf_endian32l(header + 28);

	mp_seek_set(&desc->mp, pal_off);
	const struct wu_st st = load_pal(desc);
	if (!wu_isok(st)) {
		return st;
	}

	if (desc->mp.pos < desc->img_off) {
		desc->maker.len = desc->img_off - desc->mp.pos;
		desc->maker.ptr = mp_slice(&desc->mp, desc->maker.len);
		if (!desc->maker.ptr) {
			return WUERR_HERE(wu_unexpected_eof);
		} else if (!desc->maker.ptr[desc->maker.len-1]) {
			--desc->maker.len;
		}
	}

	desc->nb = 1;
	if (desc->sub_off) {
		read_sub_data(desc);
	}
	return WU_OK;
}


/* This little-used thumbnail format requires an external palette to display
 * correctly. It's included solely because it's so simple. */
struct wu_st clm_load(FILE *ifp, struct wuimg *img) {
	size_t w = 0;
	/* The whole 4-bit raster is stored continously, which means
	 * that if the width is odd, we need to right-shift odd rows so
	 * that they begin on a byte boundary. */
	if (img->w % 2) {
		size_t stride = wuimg_stride(img);
		uint8_t prev = 0;
		for (size_t y = 0; y < img->h; ++y) {
			uint8_t *dst = img->data + y*stride;
			size_t r;
			if (y % 2) {
				r = fread(dst, 1, stride - 1, ifp);
				for (size_t b = 0; b < r; ++b) {
					uint8_t tmp = dst[b];
					dst[b] = (uint8_t)(
						prev << 4 | tmp >> 4
					);
					prev = tmp;
				}
				dst[r] = (uint8_t)(prev << 4);
			} else {
				r = fread(dst, 1, stride, ifp);
				prev = dst[stride-1];
			}
			if (!r) {
				break;
			}
			w += r;
		}
	} else {
		w = fmt_load_raster(img, ifp);
	}
	return wuerr_partial(w, wuimg_size(img));
}

struct wu_st clm_parse(FILE *ifp, struct wuimg *img) {
	/* CLM header:
		Offset  Type    Name
		0       u8      ???    // Always 0?
		1       u8      ???
		2       u8      ???
		3       u8      Width
		4       u8      Height
		5       u8      Data[] // 4-bit raster
	*/
	uint8_t hdr[5];
	if (fread(hdr, sizeof(hdr), 1, ifp)) {
		img->w = hdr[3];
		img->h = hdr[4];
		img->channels = 1;
		img->bitdepth = 4;
		return WU_OK;
	}
	return WUERR_HERE(wu_unexpected_eof);
}
