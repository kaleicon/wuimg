// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/tim2.h"
#include "misc/math.h"
#include "raster/fmt.h"

/* From
https://github.com/GirianSeed/tim2/blob/trunk/webtech/tim2v4b_e/tim2.txt
 * This document is very pleasant to read.

 * Important details about the PS2 GS:
https://github.com/DarrenRainey/PS2-Programming-Docs/blob/master/GS_Users_Manual.pdf
 * - For 8-bit alpha, 0x80 represents 1.0, and according to other sources
 *   higher values are legal (what does that produce, even?) (chapter 3.8.1)
 * - For RGB24 and RGBA5551, alpha expansion depends on the TEXA register.
 *   TEXA:AEM=1 interprets alpha as 0 when RGB=(0,0,0)
 *   TEXA:TA0 is the alpha value to use for RGB24 and when A=0 in RGBA5551.
 *   TEXA:TA1 is the alpha value to use when A=1 in RGBA5551.
 *   Since all samples set all fields to 0, perhaps it's fine to ignore it
 *   (chapter 7.8 and 3.4.6)
 * - 0x1555 is expanded to 8888 _without_ bit replication (chapter 3.4.6)

 * TODO:
 * - Expose user data
 * - Find CLT2 samples
 * - Find samples with multiple mipmaps
 * - Can images have multiple CLUTs?
*/

static void tim2_alpha32_expand(uint8_t *data, const size_t len) {
	const unsigned scale = (0xff << 8) / 0x80 + 1;
	for (size_t i = 3; i < len; i += 4) {
		data[i] = (uint8_t)((umin(data[i], 0x80) * scale) >> 8);
	}
}

static void tim2_rgba5551_bitfield(struct bitfield *bf) {
	bitfield_from_id(bf, 0x1555, 16);
	for (int i = 0; i < 3; ++i) {
		bf->comp[i].mul = (uint32_t)1 << BITFIELD_SHIFT << 3;
	}
}

static void tim2_line_callback(void *restrict data, const size_t len,
void *restrict ptr) {
	const uintptr_t depth = (uintptr_t)ptr;
	if (depth == 16) {
		endian_loop16(data, little_endian, len/2);
	} else if (depth == 32) {
		tim2_alpha32_expand(data, len);
	}
}

static bool tim2_get_linear_clut(uint8_t *restrict clut, unsigned z,
unsigned elems, struct tim2_desc *desc) {
	if (desc->pal_compound) {
		/* Groups of 8 entries are in Z order. Swap the lower two bits
		 * of the dest idx to convert to linear. */
		const unsigned groups = umax(elems/8, 32/8);
		size_t k = 0;
		for (unsigned i = 0; i < groups; ++i) {
			unsigned lo = (i & 1) << 1 | (i & 2) >> 1;
			unsigned tgt = (i & ~0x3u) | lo;
			k += fread(clut + tgt*z*8, z*8, 1, desc->ifp);
		}
		return k == groups;
	}
	return fread(clut, z * elems, 1, desc->ifp);
}

struct wu_st tim2_load(struct tim2_desc *desc, struct wuimg *img) {
	if (desc->mipmaps) {
		const uintptr_t depth = img->channels * img->bitdepth;
		size_t r = fmt_load_raster_callback(img, desc->ifp,
			tim2_line_callback, (void *)depth);
		if (!desc->pal_depth) {
			return wuerr_partial(r, wuimg_size(img));
		}
	} else if (img->bitdepth == 4) {
		for (unsigned i = 0; i < 4*4/2; ++i) {
			img->data[i] = (uint8_t)(i*0x22 | 0x10);
		}
	} else {
		for (unsigned i = 0; i < 1u << img->bitdepth; ++i) {
			img->data[i] = (uint8_t)i;
		}
	}
	unsigned z = desc->pal_depth + 1;
	unsigned elems = 1 << img->bitdepth;
	uint8_t *dst = (uint8_t *)img->u.palette->color;
	uint8_t *clut = dst + (4 - z)*elems;
	if (!tim2_get_linear_clut(clut, z, elems, desc)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	switch (z) {
	case 4: tim2_alpha32_expand(dst, elems*4); break;
	case 3: palette_from_rgb8(img->u.palette, clut, elems); break;
	case 2:
		;struct bitfield bf;
		tim2_rgba5551_bitfield(&bf);
		bitfield_unpack(&bf, dst, clut, elems);
		break;
	}
	return WU_OK;
}

struct wu_st tim2_next(struct tim2_desc *desc, struct wuimg *img) {
	/* Picture overview:
		PictHeader
		MipmapHeader
		UserSpace
		ImageData
		CLUTData

	 * PictHeader:
		Offset  Type    Name
		0       u32     TotalSize
		4       u32     CLUTSize
		8       u32     ImageSize
		12      u16     HeaderSize [*]
		14      u16     CLUTColors
		16      u8      PictFormat
		17      u8      MipMaps
		18      u8      CLUTType
		19      u8      ImageType
		20      u16     Width
		22      u16     Height
		24      u64     GSTex0
		32      u64     GSTex1
		40      u32     GSTexaFbaPabe
		44      u32     GSTexCLUT
		48
	 * [*] PictHeader + MipmapHeader + UserSpace
	*/
	fseek(desc->ifp, desc->next_off, SEEK_SET);

	uint8_t hdr[48];
	if (!fread(hdr, sizeof(hdr), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	uint32_t total = buf_endian32l(hdr);
	uint32_t clut = buf_endian32l(hdr + 4);
	uint32_t image = buf_endian32l(hdr + 8);
	uint16_t header = buf_endian16l(hdr + 12);
	const bool ok = total > clut
		&& total - clut > image
		&& total - clut - image == header
		&& header >= sizeof(hdr);
	const char *err = NULL;
	if (!ok) {
		err = "section sizes mismatch";
	} else if (hdr[16]) {
		err = "picture format != 0";
	} else if (hdr[17] > 7) {
		err = "mipmap nr > 7";
	}
	if (err) {
		return wuerr(wu_invalid_header, err);
	}
	desc->next_off += (long)total;
	desc->pal_off = desc->next_off - (long)clut;
	desc->texa_fba_pabe = buf_endian32l(hdr + 40);
	desc->mipmaps = hdr[17];
	fseek(desc->ifp, (long)(header - sizeof(hdr)), SEEK_CUR);

	img->w = buf_endian16l(hdr + 20);
	img->h = buf_endian16l(hdr + 22);
	img->channels = 1;
	img->bitdepth = 8;
	switch (hdr[19]) {
	case 1:
		img->bitdepth = 16;
		struct bitfield *bf = wuimg_bitfield_init(img);
		if (!bf) {
			return WUERR_HERE(wu_alloc_error);
		}
		tim2_rgba5551_bitfield(bf);
		/* FIXME: 0x1555 bitfields are always interpreted as GL_RGB5_A1,
		 * meaning our mul change is ignored when viewing (but not when
		 * software-converting).
		 * Unlikely anyone will notice or care though, unless they're
		 * developing for the PS2. */
		break;
	case 2:
		img->channels = 3;
		break;
	case 3:
		img->channels = 4;
		break;
	case 4:
		img->bitdepth = 4;
		img->bit = little_endian;
		// fallthrough
	case 5:
		;uint16_t clut_nr = buf_endian16l(hdr + 14);
		if (!clut_nr) {
			return wuerr(wu_invalid_header, "empty palette");
		} else if (clut_nr % (1 << img->bitdepth)) {
			return wuerr(wu_invalid_header, "odd palette size");
		}
		bool csm2 = hdr[18] & 0x80;
		if (img->bitdepth == 8) {
			desc->pal_compound = !csm2;
		} else {
			desc->pal_compound = !csm2 & ((hdr[18] & 0x40) != 0);
		}
		desc->pal_elems = clut_nr;
		desc->pal_depth = hdr[18] & 0x3f;
		switch (desc->pal_depth) {
		case 1: case 2: case 3: break;
		default: return wuerr(wu_invalid_header,
			desc->pal_depth
				? "unknown palette type"
				: "4- or 8-bit image but there's no palette");
		}
		if (!wuimg_palette_init(img)) {
			return WUERR_HERE(wu_alloc_error);
		}
		break;
	default: return wuerr(wu_invalid_header, "unknown image type");
	}
	if (!desc->mipmaps) {
		if (!desc->pal_depth) {
			return wuerr(wu_invalid_header, "no CLUT and no image");
		}
		img->w = 1 << (img->bitdepth/2);
		img->h = img->w;
	}
	++desc->idx;
	return WU_OK;
}

struct wu_st tim2_init(struct tim2_desc *desc, FILE *ifp) {
	/* TIM2 header:
		Offset  Type    Name
		0       u8      Magic[4]
		4       u8      Version
		5       u8      ID
		6       u16     Pictures
		8
	*/

	const uint8_t tim2[4] = {'T', 'I', 'M', '2'};
	const uint8_t clt2[4] = {'C', 'L', 'T', '2'};
	uint8_t hdr[8];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, tim2, sizeof(tim2))
	&& memcmp(hdr, clt2, sizeof(clt2))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	switch (hdr[5]) {
	case 0: case 1:
		;const align_t align = hdr[5] ? 7 : 4;
		*desc = (struct tim2_desc) {
			.ifp = ifp,
			.next_off = 1 << align,
			.version = hdr[4],
			.align = align,
			.nr = buf_endian16l(hdr + 6),
		};
		return WU_OK;
	}
	return wuerr(wu_invalid_header, "unknown data alignment (format id > 1)");
}
