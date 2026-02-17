// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <stdlib.h>

#include "misc/common.h"
#include "raster/fmt.h"
#include "lib/kaboom.h"

void bmb_cleanup(struct bmb_desc *desc) {
	free((void *)desc->idx2.mem);
}

static struct wu_st end(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	(void)iff; (void)ptr; (void)chunk;
	return WU_OK;
}

static struct wu_st clut(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	(void)iff;
	if (chunk.len % 4) {
		return wuerr(wu_invalid_header, "CLUT size not multiple of 4");
	} else if (chunk.len / 4 > 256) {
		return wuerr(wu_invalid_header, "CLUT has more than 256 entries");
	}
	struct bmb_desc *desc = ptr;
	palette_from_file(desc->img->u.palette, 4, chunk.len/4, desc->ifp, 8);
	--desc->iff.table;
	return iff_next_FILE(&desc->iff, desc->ifp, chunk);
}

static const struct iff_table bmb_end_table[] = {
	{.id = FOURCC('E', 'N', 'D', ' '), .fn = end},
	{.id = FOURCC('C', 'L', 'U', 'T'), .fn = clut},
};

struct wu_st bmb_decode(struct bmb_desc *desc, struct wuimg *img) {
	struct wu_st st = fmt_load_raster_st(img, desc->ifp);
	if (!wu_isok(st)) {
		return st;
	}
	++desc->i;
	fseek(desc->ifp, (long)(desc->imag_size - wuimg_size(img)), SEEK_CUR);
	desc->iff.table = bmb_end_table + (img->mode == image_mode_palette);
	desc->iff.table_len = 1;
	return iff_next_FILE(&desc->iff, desc->ifp, (struct iff_chunk){0});
}

static struct wu_st imag(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	// IMAG contains the raw raster followed by mipmaps, if any
	(void)iff; (void)chunk;
	struct bmb_desc *desc = ptr;
	desc->imag_size = chunk.len;

	struct wutree *meta = wuimg_get_metadata(desc->img);
	mp_seek_cur(&desc->idx2, 2);
	struct wuptr name;
	const bool has_name = mp_upto(&desc->idx2, &name, '\0');
	if (meta) {
		if (has_name) {
			tree_add_leaf_len(meta, "Name", name, NULL);
		}
		if (desc->mipmaps) {
			tree_bud_leaf_u(meta, "Mipmaps", desc->mipmaps);
		}
	}
	return WU_OK;
}

static struct wu_st mipm(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	/* MIPM chunk:
		0       u32     ???
		4       u32     ???
		8       u32     ???
		12      u32     Levels?
		16
	*/
	struct bmb_desc *desc = ptr;
	uint32_t hdr[4];
	if (chunk.len != sizeof(hdr)) {
		return wuerr(wu_invalid_header, "MIPM chunk size != 16");
	} else if (!fread(hdr, sizeof(hdr), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	desc->mipmaps = endian32l(hdr[3]);
	desc->iff.table += 1;
	desc->iff.table_len = 1;
	return iff_next_FILE(iff, desc->ifp, chunk);
}

static bool info_good_dims(uint8_t hdr[static 10]) {
	const uint16_t td = buf_endian16l(hdr);
	const uint16_t tlog2 = buf_endian16l(hdr + 4);
	const uint16_t d = buf_endian16l(hdr + 8);
	return d <= td && tlog2 < 16 && td == 1u << tlog2;
}

static struct wu_st info(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	/* INFO chunk:
		Offset  Type    Name
		0       u16     TexWidth
		2       u16     TexHeight
		4       u16     TexWidthLog2?
		6       u16     TexHeightLog2?
		8       u16     Width
		10      u16     Height
		12      u16     ???      // Always 0x0001?
		14      u8      ???[6]   // Always 0?
		20      u8      Bitdepth
		21      u8      ???[11]  // (08|00) (01{3}|00{3}) 00+
		32
	*/
	struct bmb_desc *desc = ptr;
	uint8_t hdr[32];
	if (chunk.len != sizeof(hdr)) {
		return wuerr(wu_invalid_header, "INFO chunk size != 32");
	} else if (!fread(hdr, sizeof(hdr), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (!info_good_dims(hdr) || !info_good_dims(hdr + 2)) {
		return wuerr(wu_invalid_header,
			"mismatch between BMB texture and raster dimensions");
	}
	desc->img->w = buf_endian16l(hdr + 8);
	desc->img->h = buf_endian16l(hdr + 10);
	desc->img->channels = hdr[20]/8;
	desc->img->bitdepth = 8;
	desc->img->align_sh = (align_t)buf_endian16l(hdr + 4);
	desc->img->layout = pix_bgra;
	switch (hdr[20]) {
	case 8:
		if (!wuimg_palette_init(desc->img)) {
			return WUERR_HERE(wu_alloc_error);
		}
		break;
	case 24: case 32: break;
	default: return wuerr(wu_invalid_header, "bitdepth is not 8, 24, nor 32");
	}
	desc->iff.table += 1;
	desc->iff.table_len = 2;
	return iff_next_FILE(iff, desc->ifp, chunk);
}

static const struct iff_table bmb_image_info[] = {
	{.id = FOURCC('I', 'N', 'F', 'O'), .fn = info},
	{.id = FOURCC('M', 'I', 'P', 'M'), .fn = mipm},
	{.id = FOURCC('I', 'M', 'A', 'G'), .fn = imag},
};

struct wu_st bmb_parse_next(struct bmb_desc *desc, struct wuimg *img) {
	desc->iff.table = bmb_image_info;
	desc->iff.table_len = 1;
	desc->img = img;
	desc->mipmaps = 0;
	return iff_next_FILE(&desc->iff, desc->ifp, (struct iff_chunk){0});
}

static struct wu_st idx2(struct iff_state *iff, void *ptr,
const struct iff_chunk chunk) {
	/* IDX2 chunk:
		Offset  Type    Name
		0       u16     Index   // Always sequential
		2       char    Name[]  // nul terminated
	 * Repeat for each image. There's an extra 0xffff at the end.
	*/
	(void)iff;
	struct bmb_desc *desc = ptr;
	uint8_t *data = small_malloc(chunk.len, 1);
	if (!data) {
		return WUERR_HERE(wu_alloc_error);
	}
	desc->idx2 = mp_mem(chunk.len, data);
	if (!fread(data, chunk.len, 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	return WU_OK;
}

static const struct iff_table bmb_init_table[] = {
	{.id = FOURCC('I', 'D', 'X', '2'), .fn = idx2},
};

struct wu_st bmb_init(struct bmb_desc *desc, FILE *ifp) {
	/* BMB layout:
		NrImages  u32
		Names     (IDX2)
	 * Then for each image:
		Image info        (INFO)
		Mipmap info       (MIPM), optional
		Raster and mipmap (IMAG)
		EOF               (END )
	*/
	*desc = (struct bmb_desc) {
		.ifp = ifp,
		.iff = {
			.table = bmb_init_table,
			.table_len = ARRAY_LEN(bmb_init_table),
			.user = desc,
			.endian = little_endian,
			.id_endian = big_endian,
			.align_sh = 0,
		},
	};
	if (!fread(&desc->nr, sizeof(desc->nr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	desc->nr = endian32l(desc->nr);
	return iff_next_FILE(&desc->iff, ifp, (struct iff_chunk){0});
}
