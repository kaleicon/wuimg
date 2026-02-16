// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "lib/pc98.h"

static uint32_t bitspread(uint32_t c) {
	/* Spreads a byte's bits so that they're 4 bits apart. */
	c *= 0x40100401; // replicate every 10 bits
	c &= 0xc0c0c0c0; // isolate bit pairs
	c |= c >> 3;     // replicate bit pairs at an offset
	c &= 0x88888888; // isolate bits in {1,0, 3,2, 5,4, 7,6} order
	return c;        // on Little-Endian, we're done. on BE, swap later
}

/* Kirara/IDES PRS
https://gitlab.com/bunnylin/supersakura/-/blob/dev/doc/gfx/prs-ada-mda.md
 * Also handle Micro Cabin PRS as it's hard to disambiguate between
 * the two, as if a Summer Mirage.
 * TODO: Some Micro Cabin images come out truncated. A bad rip?
*/

static struct wu_st micro_cabin_prs_decode(const struct prs_desc *desc,
struct wuimg *img) {
	const size_t dst_len = img->w/8 * img->h;
	uint32_t *dst = (uint32_t *)img->data;
	size_t d = 0;
	const struct wuptr src = desc->bytes;
	size_t s = 0;
	while (s < src.len) {
		const uint8_t op = src.ptr[s];
		++s;
		unsigned count = op & 0x3;
		if (!count) {
			if (s >= src.len) {
				break;
			}
			count = ((src.ptr[s] - 1) & 0xff) + 1;
			++s;
		}
		if (dst_len - d < count) {
			break;
		}

		const unsigned pix_size = 3;
		const unsigned off = op >> 2;
		if (off == 63) {
			if (src.len - s < pix_size * count) {
				break;
			}
			for (unsigned k = 0; k < count; ++k) {
				uint32_t a = 0;
				for (unsigned i = 0; i < pix_size; ++i) {
					a |= bitspread(src.ptr[s])
						>> i;
					++s;
				}
				dst[d] = a;
				++d;
			}
		} else {
			uint32_t a = 0;
			for (unsigned i = 0; i < pix_size; ++i) {
				a |= bitspread(desc->u.dict[off + i*63]) >> i;
			}
			for (unsigned k = 0; k < count; ++k) {
				dst[d] = a;
				++d;
			}
		}
	}
	return wuerr_partial(d, dst_len);
}

struct prs_column_state {
	struct bitstrm bs;
	struct wuptr bytes;
	size_t pos;
	uint8_t *buf;
	uint8_t r;
	uint8_t pat;
	uint8_t ring[256];
};

static size_t prs_column_expand(struct prs_column_state *cs, size_t buf_wanted,
size_t excess) {
	memmove(cs->buf, cs->buf + buf_wanted, excess);
	size_t d = excess;
	while (d < buf_wanted) {
		uint32_t bits = ~bitstrm_lsb_peek_32(&cs->bs);
		unsigned b = 0;
		while (d < buf_wanted && b < 32 - 5) {
			uint32_t n = bit_clz32(bits << b);
			unsigned count = 1;
			switch (n) {
			uint8_t next;
			case 0:
				if (cs->pos == cs->bytes.len) {
					return d;
				}
				next = cs->bytes.ptr[cs->pos];
				cs->buf[d] = next;
				cs->ring[cs->r] = next;
				++cs->r;
				++cs->pos;
				break;
			case 1:
				if (cs->bytes.len - cs->pos < 2) {
					return d;
				}
				count = cs->bytes.ptr[cs->pos];
				next = cs->bytes.ptr[cs->pos+1];
				cs->pos += 2;
				if (count >= 0xf0) {
					count = ((count - 1) & 0xf) + 1;
					size_t rem = 256 - next;
					size_t c1 = zumin(count, rem);
					memcpy(cs->buf + d, cs->ring + next,
						c1);
					memcpy(cs->buf + d + c1, cs->ring,
						count - c1);
				} else {
					memset(cs->buf + d, next, count);
				}
				break;
			case 2:
				if (cs->bytes.len - cs->pos < 3) {
					return d;
				}
				count = cs->bytes.ptr[cs->pos];
				++cs->pos;
				memtessel(cs->buf + d, cs->bytes.ptr + cs->pos,
					2, count);
				cs->pos += 2;
				break;
			case 3:
				cs->buf[d] = 0;
				cs->ring[cs->r] = 0;
				++cs->r;
				break;
			default:
				n = 3;
				if (cs->pos == cs->bytes.len) {
					return d;
				}
				count = cs->bytes.ptr[cs->pos];
				++cs->pos;
				memset(cs->buf + d, cs->pat, count);
				break;
			}
			d += count;
			b += n + 1;
		}
		bitstrm_seek(&cs->bs, b);
	}
	return d;
}

struct wu_st prs_decode(const struct prs_desc *desc, struct wuimg *img) {
	if (desc->micro_cabin) {
		return micro_cabin_prs_decode(desc, img);
	}
	const size_t height = img->h * desc->u.ides.ch;
	struct prs_column_state cs = {
		.bytes = desc->bytes,
		.buf = calloc(height + 0xfe, 1),
		.pat = desc->u.ides.pat,
	};
	if (!cs.buf) {
		return WUERR_HERE(wu_alloc_error);
	}
	bitstrm_from_wuptr(&cs.bs, desc->u.ides.bits);

	const size_t bands = img->w/8;
	size_t band = 0;
	size_t r = 0;
	while (band < bands) {
		r = prs_column_expand(&cs, height, r);
		size_t y = 0;
		while (y < img->h) {
			uint32_t *dst = (uint32_t *)img->data + y*bands + band;
			size_t k = 0;
			uint32_t a = 0;
			for (int i = 0; i < 3; ++i) {
				if (desc->u.ides.plane_mask & (1 << i)) {
					a |= bitspread(cs.buf[k*img->h+y])
						>> i;
					++k;
				}
			}
			*dst = endian32l(a);
			++y;
		}
		if (r < height) {
			break;
		}
		r -= height;
		++band;
	}
	free(cs.buf);
	return wuerr_partial(band, bands);
}

static struct wu_st micro_cabin_prs_parse(struct prs_desc *desc,
struct mparser *mp, const uint8_t header[static 8], struct wuimg *img) {
	img->w = buf_endian16l(header + 2) * 8;
	img->h = buf_endian16l(header + 4);
	desc->u.dict = mp_slice(mp, 63*3);
	desc->bytes = mp_remaining(mp);
	return desc->u.dict && desc->bytes.len
		? WU_OK : WUERR_HERE(wu_unexpected_eof);
}

struct wu_st prs_parse(struct prs_desc *desc, const struct wuptr mem,
struct wuimg *img) {
	/* IDES PRS header:
		Offset  Type    Name
		0       u8      Width  // multiply by 8
		1       u8      Height
		2       u8      X      // multiply by 8
		3       u8      Y
		4.0     bool    HasBlue
		4.1     bool    HasRed
		4.2     bool    HasGreen
		4.3     bool    Unused?
		4.4     u3      TransparentIdx  // game may override this
		4.7     bool    Unused?
		5       u16     DataBytesSize
		7       u8      DefaultPattern
		8
	 * Header is followed by a DataByte section, then a CommandBits
	 * section.

	 * Micro Cabin PRS:
		0       u16      ???
		2       u16      Width
		4       u16      Height
		6       u16      ???
		8       u8       BRGDict[3][63]
		195

	 * Decoded pixels must be doubled vertically for both formats.
	*/
	struct mparser mp = mp_wuptr(mem);
	const uint8_t *header = mp_slice(&mp, 8);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	img->channels = 4;
	img->bitdepth = 1;
	img->layout = pix_layout_pack(1, 2, 0, 3);
	img->alpha = alpha_ignore;
	wuimg_aspect_ratio(img, 1, 2);

	const uint8_t plane_mask = header[4] & 0xf;
	const uint8_t trans = header[4] >> 4;
	const uint16_t byte_section_size = buf_endian16l(header + 5);

	/* This doesn't always work, but without fancy statistical modelling,
	 * peeking at the surrounding filesystem, or asking the user to do
	 * either of the former and tell us the result, it's the best we can
	 * do. */
	desc->micro_cabin = !header[0] || !header[1]
		|| (header[4] & 0x88) || !(plane_mask & 0x7)
		|| byte_section_size >= mem.len - 8;
	if (desc->micro_cabin) {
		return micro_cabin_prs_parse(desc, &mp, header, img);
	}
	img->w = header[0] * 8;
	img->h = header[1];
	desc->u.ides = (struct prs_ides) {
		.x = header[2],
		.y = header[3],
		.ch = (uint8_t)(
			(plane_mask & 0x1)
			+ ((plane_mask >> 1) & 0x1)
			+ ((plane_mask >> 2) & 0x1)
		),
		.plane_mask = plane_mask,
		.trans = trans >> 4,
		.pat = header[7],
	};
	desc->bytes = mp_avail(&mp, byte_section_size),
	desc->u.ides.bits = mp_remaining(&mp);
	return desc->u.ides.bits.len ? WU_OK : WUERR_HERE(wu_unexpected_eof);
}

/* IDES PC-98 GPC

 * Decoding algorithm from
https://github.com/HolografixFinn/gpc2bmp
 * plus original research.

 * TODO: What's wrong with Himitu_c/pic/s53.gpc (from 秘密の花園)? Did I get a
 *       bad rip too?
*/

struct gpc_row_state {
	struct wuptr src;
	size_t s;
	size_t row_stride;
	uint8_t *buf;
	uint8_t end[(((1 + 8) * 8) + 1) * 2];
};

void gpc_cleanup(struct gpc_desc *desc) {
	palette_unref(desc->pal);
}

static size_t gpc_row_expand(struct gpc_row_state *rs, uint8_t *restrict dst,
const uint8_t *restrict prev, const size_t dst_len, const size_t excess) {
	memcpy(dst, prev + dst_len, excess);
	size_t d = excess;
	const uint8_t *src = rs->src.ptr;
	size_t src_len = rs->src.len;
	size_t s = rs->s;
	while (d < dst_len) {
		if (src_len - s < sizeof(rs->end)/2) {
			if (src == rs->end) {
				break;
			}
			src = mem_bufswitch(src, &s,
				&src_len, rs->end, sizeof(rs->end));
		}

		uint8_t iflags = src[s];
		++s;
		for (size_t i = 0; i < 8; ++i, iflags <<= 1) {
			const size_t pos = d + i*8;
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
		d += 64;
	}
	rs->src.ptr = src;
	rs->src.len = src_len;
	rs->s = s;
	return d;
}

static size_t gpc_unpack(struct wuimg *img, struct gpc_row_state *rs,
const size_t plane_len, const size_t row_size, const size_t row_skip) {
	uint32_t *dst = (uint32_t *)img->data;
	const size_t dwords = wuimg_stride(img)/sizeof(*dst);
	size_t cycle = 0;
	size_t dst_y = cycle;
	size_t r = 0;
	size_t total = 0;
	for (size_t y = 0; y < img->h; ++y) {
		/* Rows are split into 4 bitplanes, plus an extra byte at the
		 * start. */
		uint8_t *restrict row = rs->buf + rs->row_stride * (y & 1);
		uint8_t *restrict prev = rs->buf + rs->row_stride * !(y & 1);
		r = gpc_row_expand(rs, row, prev, row_size, r);
		const uint8_t xskip = row[0];
		if (xskip) {
			/* XOR horizontally, with `xskip` bytes of stride. On
			 * reaching the end, wrap around and do the same to the
			 * bytes in between. `xor` is reused when wrapping. */
			uint8_t xor = 0;
			for (size_t i = 0; i < xskip; ++i) {
				for (size_t x = i+1; x < row_size; x += xskip) {
					xor ^= row[x];
					row[x] = xor;
				}
			}
		}

		/* XOR with the previous row. */
		for (size_t x = 1; x < row_size; ++x) {
			row[x] ^= prev[x];
		}

		/* Merge bitplanes into 4-bit quantities, lower planes into
		 * least-significant positions. and write to every `row_skip`
		 * row of the output buffer. On reaching the end, wrap around
		 * to fill the next row group. */
		uint32_t *dst_row = dst + dst_y * dwords;
		for (size_t i = 0; i < plane_len; ++i) {
			/* We can handle 4-bit data, so pack 8 pixels into
			 * u32 words. */
			uint32_t a = 0;
			for (size_t k = 0; k < 4; ++k) {
				a |= bitspread(row[1 + plane_len*k + i])
					>> (3 - k);
			}
			dst_row[i] = endian32l(a);
		}
		dst_y += row_skip;
		if (dst_y >= img->h) {
			++cycle;
			dst_y = cycle;
		}

		total += r;
		if (r < row_size) {
			break;
		}
		r -= row_size;
	}
	return total;
}

struct wu_st gpc_decode(const struct gpc_desc *desc, struct wuimg *img) {
	const size_t plane_len = strip_base(img->w, 1);
	const size_t row_size = plane_len * 4 + 1;
	/* Decompressor writes 64 bytes per iteration, so add some padding to
	 * both rows. */
	struct gpc_row_state rs = {
		.row_stride = row_size + 63,
	};
	rs.buf = calloc(rs.row_stride, 2);
	size_t w = 0;
	if (rs.buf) {
		rs.src = mp_avail_at(&desc->mp, desc->mp.pos,
			desc->cur.comp_len);
		w = gpc_unpack(img, &rs, plane_len, row_size,
			desc->cur.row_skip);
		free(rs.buf);
	}
	const size_t max = row_size * img->h;
	return wuerr_partial(zumin(w, max), max);
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
