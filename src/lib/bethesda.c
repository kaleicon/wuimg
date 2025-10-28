// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "lib/bethesda.h"
#include "misc/math.h"
#include "misc/common.h"
#include "misc/decomp.h"
#include "misc/iff.h"
#include "raster/fmt.h"

/* Bethesda GXA and BSI.
 * These follow IFF, with the important caveat that although chunk lengths are
 * big-endian, contents are little-endian. Great obfuscation everyone, got me
 * stumpted longer than the RLE algo. */

/* Common functions */
static struct wu_st load_pal(struct palette **pal, FILE *ifp,
const uint32_t chunk_len) {
	if (chunk_len == 0x300) {
		*pal = palette_new();
		if (*pal) {
			return palette_from_file(*pal, 3, 256, ifp, 6)
				? WU_OK : WUERR_HERE(wu_unexpected_eof);
		}
		return WUERR_HERE(wu_alloc_error);
	}
	return wuerr(wu_invalid_header, "palette length != 256*3");
}

/* FNT (FNHD) */
void fnhd_cleanup(struct fnhd_desc *desc) {
	palette_unref(desc->pal);
}

struct wu_st fnhd_load_glyph(struct fnhd_desc *desc, struct wuimg *img) {
	++desc->cur;
	return fmt_load_raster_st(img, desc->ifp);
}

struct wu_st fnhd_next_glyph(struct fnhd_desc *desc, struct wuimg *img) {
	/* "FBMP" is an array of variable-sized characters, whose structure is:
		Offset  Type    Name
		0       u16     ???     // Metrics?
		2       u16     ???
		4       u16     ???
		6       u16     Width
		8       u16     Height
		10      u8      Raster[Width*Height]
	*/
	const uint8_t end[4] = "END ";
	uint16_t buf[5];
	const size_t r = fread(buf, 1, sizeof(buf), desc->ifp);
	if (r >= sizeof(end) && !memcmp(buf, end, sizeof(end))) {
		return WU_NO_CHANGE;
	} else if (r < sizeof(buf)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	img->w = endian16l(buf[3]);
	img->h = endian16l(buf[4]);
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 6;
	wuimg_palette_set(img, palette_ref(desc->pal));
	return WU_OK;
}

static struct wu_st fbmp(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	(void)iff; (void)ptr; (void)chunk;
	return WU_OK;
}

static struct wu_st fpal(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	/* "FPAL" and "BPAL" contents:
		Offset  Type    Name
		0       u8      Palette[256][3]
		256*3
	*/

	struct fnhd_desc *desc = ptr;
	const struct wu_st st = load_pal(&desc->pal, desc->ifp, chunk.len);
	desc->pal->color[0].a = 0;
	iff->table += 2;
	iff->table_len = 1;
	return wu_isok(st) ? iff_next_FILE(iff, desc->ifp, chunk) : st;
}

static struct wu_st fnhd(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	/* "FNHD" contents:
		Offset  Type    Name
		0       u8      Description[40]
		40      u16     ???[4]
		48      u16     NrGlyphs
		50      u8      ???[6]
		56
	 * Description is null terminated, and seems to be followed by garbage.
	*/
	struct fnhd_desc *desc = ptr;
	uint16_t buf[8];
	if (chunk.len != 0x38) {
		return wuerr(wu_invalid_header, "FNHD length != 0x38");
	} else if (!fread(desc->desc, sizeof(desc->desc), 1, desc->ifp)
	|| !fread(buf, sizeof(buf), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	desc->glyphs = endian16l(buf[4]);
	++iff->table;
	iff->table_len = 2;
	return iff_next_FILE(iff, desc->ifp, chunk);
}

static const struct iff_table fnhd_table[] = {
	{.id = FOURCC('F', 'N', 'H', 'D'), .fn = fnhd},
	{.id = FOURCC('B', 'P', 'A', 'L'), .fn = fpal}, // BPAL and FPAL are
	{.id = FOURCC('F', 'P', 'A', 'L'), .fn = fpal}, // exactly the same
	{.id = FOURCC('F', 'B', 'M', 'P'), .fn = fbmp},
};

struct wu_st fnhd_init(struct fnhd_desc *desc, FILE *ifp) {
	/* FNT layout:
		Header (FNHD)
		Pal    (BPAL or FPAL)
		Bitmap (FBMP)
		EOF    (END )
	*/
	desc->ifp = ifp;
	desc->pal = NULL;
	desc->cur = 0;

	struct iff_state iff = {
		.table = fnhd_table,
		.table_len = 1,
		.user = desc,
		.endian = big_endian,
	};
	return iff_next_FILE(&iff, ifp, (struct iff_chunk){0});
}


/* GXA (BMHD) */
const char * gxa_compression_str(const enum gxa_compression c) {
	switch (c) {
	case gxa_none: return "None";
	case gxa_rle: return "RLE";
	case gxa_mystery2: return "Unknown (0x02)";
	}
	return "???";
}

void gxa_cleanup(struct gxa_desc *desc) {
	palette_unref(desc->pal);
}

struct wu_st gxa_load_image(struct gxa_desc *desc, struct wuimg *img) {
	++desc->cur;
	const size_t size = wuimg_size(img);
	switch (desc->compression) {
	case gxa_none:
		return fmt_load_raster_st(img, desc->ifp);
	case gxa_rle:
		;uint8_t *rle = malloc(desc->data_len);
		if (rle) {
			const size_t r = decomp_topbitrle(img->data,
				size, rle,
				fread(rle, 1, desc->data_len, desc->ifp), 1);
			free(rle);
			return wuerr_partial(r, size);
		}
		return WUERR_HERE(wu_alloc_error);
	case gxa_mystery2: break;
	}
	return wuerr(wu_unsupported_feature, NULL);
}

struct wu_st gxa_next_image(struct gxa_desc *desc, struct wuimg *img) {
	/* "BBMP" is a sequence of variable-size images that begin with
	 * this header:
		Offset  Type    Name
		0       u16     ???          // Always 1
		2       u16     Width
		4       u16     Height
		6       u16     ???[2]       // Always 0
		10      u16     Compression  // 0, 1, or 2
		12      u8      ???[6]       // Always 0
		18      u8      Data[]

	 * When compression is != 0, the first uint32 in Data is the
	 * compressed stream length.
	*/
	uint16_t info[9];
	if (!fread(info, sizeof(info), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	img->w = endian16l(info[1]);
	img->h = endian16l(info[2]);
	img->channels = 1;
	img->bitdepth = 8;
	img->bitrange = 6;
	desc->compression = endian16l(info[5]);
	switch (desc->compression) {
	case gxa_none: break;
	case gxa_rle:
		;uint32_t len;
		if (!fread(&len, sizeof(len), 1, desc->ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		desc->data_len = endian32l(len);
		if (desc->data_len/2 > img->w*img->h) {
			desc->data_len = (uint32_t)(img->w*img->h*2);
		}
		break;
	case gxa_mystery2:
		return wuerr(wu_unsupported_feature,
			"unknown compression method 2");
	default:
		return wuerr(wu_uncertain_validity,
			"unknown compression method > 2");
	}
	wuimg_palette_set(img, palette_ref(desc->pal));
	return WU_OK;
}

static struct wu_st bbmp(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	(void)iff; (void)ptr; (void)chunk;
	// Finish parsing.
	return WU_OK;
}

static struct wu_st bpal(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	/* "BPAL" contents:
		Offset  Type    Name
		0       u8      Palette[256][3]
		256*3
	*/

	struct gxa_desc *desc = ptr;
	const struct wu_st st = load_pal(&desc->pal, desc->ifp, chunk.len);
	++iff->table;
	return wu_isok(st) ? iff_next_FILE(iff, desc->ifp, chunk) : st;
}

static struct wu_st bmhd(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	/* "BMHD" contents:
		Offset  Type    Name
		0       u8      Comment[32]
		32      u16     NbImages
		34
	*/
	if (chunk.len != 0x22) {
		return wuerr(wu_invalid_header, "BMHD length != 0x22");
	}

	struct gxa_desc *desc = ptr;
	if (!fread(desc->comment, sizeof(desc->comment), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	uint8_t i = 32;
	while (i && desc->comment[i-1] == ' ') {
		--i;
	}
	desc->comment_len = i;

	uint16_t nb;
	if (!fread(&nb, sizeof(nb), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	desc->nb_images = endian16l(nb);

	++iff->table;
	return iff_next_FILE(iff, desc->ifp, chunk);
}

/* GXA chunks are fixed, so for brevity set table_len to 1 and increase the
 * base pointer every time we get a match. */
static const struct iff_table gxa_table[] = {
	{.id = FOURCC('B', 'M', 'H', 'D'), .fn = bmhd},
	{.id = FOURCC('B', 'P', 'A', 'L'), .fn = bpal},
	{.id = FOURCC('B', 'B', 'M', 'P'), .fn = bbmp},
};

struct wu_st gxa_init(struct gxa_desc *desc, FILE *ifp) {
	/* GXA layout:
		Header (BMHD)
		Pal    (BPAL)
		Bitmap (BBMP)
		EOF    (END )
	*/
	desc->ifp = ifp;
	desc->pal = NULL;
	desc->cur = 0;

	struct iff_state iff = {
		.table = gxa_table,
		.table_len = 1,
		.user = desc,
		.endian = big_endian,
	};
	return iff_next_FILE(&iff, ifp, (struct iff_chunk){0});
}


/* BSI */
const char * bsi_compression_str(const enum bsi_compression c) {
	switch (c) {
	case bsi_none: return "None";
	case bsi_scanlines: return "Scanlines";
	}
	return "???";
}

void bsi_cleanup(struct bsi_desc *desc) {
	free(desc->table);
	palette_unref(desc->pal);
}

static struct wu_st load_scanlines(struct bsi_desc *desc, struct wuimg *img,
const uint16_t i) {
	uint32_t *tab;
	if (!desc->table) {
		desc->table = malloc(desc->comp_len);
		if (!desc->table) {
			return WUERR_HERE(wu_alloc_error);
		}
		desc->comp_len = (uint32_t)fread(desc->table, 1,
			desc->comp_len, desc->ifp);
		if (desc->comp_len < desc->h*desc->nb_images*sizeof(*tab)) {
			return wuerr(wu_unexpected_eof, "EOF in scanline table");
		}
	}
	tab = desc->table;
	const uint8_t *src = (uint8_t *)tab;
	tab += i*desc->h;
	size_t written = 0;
	for (size_t y = 0; y < desc->h; ++y) {
		uint32_t off = endian32l(tab[y]);
		off = u32min(off, desc->comp_len);
		uint32_t len = u32min(desc->w, desc->comp_len - off);
		uint8_t *dst = img->data + y*img->w;
		memcpy(dst, src + off, len);
		// wuimg_alloc() uses calloc(), so this is redundant
		//memset(dst + len, 0, desc->w - len);
		written += len;
	}
	return wuerr_partial(written, img->w*img->h);
}

struct wu_st bsi_load_image(struct bsi_desc *desc, struct wuimg *img, uint16_t i) {
	switch (desc->compression) {
	case bsi_none:
		fseek(desc->ifp, desc->pos + desc->w*desc->h*i, SEEK_SET);
		return fmt_load_raster_st(img, desc->ifp);
	case bsi_scanlines:
		return load_scanlines(desc, img, i);
	}
	return WUERR_HERE(wu_unsupported_feature);
}

struct wu_st bsi_set_image(struct bsi_desc *desc, struct wuimg *img) {
	img->w = desc->w;
	img->h = desc->h;
	img->channels = 1;
	img->bitdepth = 8;
	if (desc->pal) {
		img->bitrange = 6;
		wuimg_palette_set(img, palette_ref(desc->pal));
	}
	return WU_OK;
}

static struct wu_st data(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	/* "DATA" contents:
		Offset  Type    Name
		0       u8      Raster[Height][Width]
		Width*Height
	*/
	(void)iff; (void)chunk;
	struct bsi_desc *desc = ptr;
	if (desc->compression == bsi_scanlines) {
		uint32_t entries = desc->h*desc->nb_images;
		if (entries > chunk.len/sizeof(uint32_t)) {
			return wuerr(wu_invalid_header, "truncated DATA chunk");
		}
		uint32_t tablen = entries*sizeof(uint32_t);
		uint32_t scanlines = u32min(entries, (chunk.len - tablen)/desc->w);
		desc->comp_len = u32min(chunk.len, tablen + scanlines*desc->w);
	} else {
		desc->pos = ftell(desc->ifp);
	}
	return WU_OK;
}

static struct wu_st cmap(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	/* "CMAP" contents:
		Offset  Type    Name
		0       u8      Palette[256][3]
		256*3
	*/

	struct bsi_desc *desc = ptr;
	const struct wu_st st = load_pal(&desc->pal, desc->ifp, chunk.len);
	++iff->table;
	iff->table_len = 1;
	return wu_isok(st) ? iff_next_FILE(iff, desc->ifp, chunk) : st;
}

static const struct iff_table bsi_table[] = {
	{.id = FOURCC('C', 'M', 'A', 'P'), .fn = cmap},
	{.id = FOURCC('D', 'A', 'T', 'A'), .fn = data},
};

static struct wu_st bhdr(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	/* "BHDR" contents:
		Offset  Type    Name
		0       u16     ???
		2       u16     ???
		4       u16     Width
		6       u16     Height
		8       u16     ???         // 0 or 1 for IFHD, 0x900 for BSIF
		10      ??      ???[4]      // Always 0?
		14      u16     NbImages
		16      u16     ???         // Almost always 0x47
		18      u16     ???         // Between 0 and 3
		20      u16     ???         // Always 0?
		22      u16     ???         // Almost always 0x100
		24      u16     Compression // 0 or 4
		26
	*/
	uint16_t buf[13];
	struct bsi_desc *desc = ptr;
	if (chunk.len != 0x1a) {
		return wuerr(wu_invalid_header, "BHDR length != 0x1a");
	} else if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const enum bsi_compression compression = endian16l(buf[12]);
	switch (compression) {
	case bsi_none: case bsi_scanlines:
		desc->w = endian16l(buf[2]);
		desc->h = endian16l(buf[3]);
		desc->nb_images = endian16l(buf[7]);
		desc->compression = compression;
		if (!desc->w || !desc->h) {
			return wuerr(wu_invalid_header, "width or height == 0");
		} else if (!desc->nb_images) {
			return WUERR_HERE(wu_no_image_data);
		}
		if (desc->bsif) {
			if (compression != bsi_none) {
				return wuerr(wu_uncertain_validity,
					"BSIF file with compression method != 0");
			}
			// BSIF files have no CMAP
			iff->table = bsi_table + 1;
			iff->table_len = 1;
		} else {
			iff->table = bsi_table;
			iff->table_len = ARRAY_LEN(bsi_table);
		}
		return iff_next_FILE(iff, desc->ifp, chunk);
	}
	return wuerr(wu_uncertain_validity, "unknown compression method");
}

static const struct iff_table bsi_bitmap_header[] = {
	{.id = FOURCC('B', 'H', 'D', 'R'), .fn = bhdr},
};

static struct wu_st ifhd(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	/* "IFHD" contents:
		Offset  Type    Name
		0       u8      One       // Always 1?
		1       u8      Zeros[43] // Always 0?
		44
	*/
	if (chunk.len != 0x2c) {
		return wuerr(wu_invalid_header, "IFHD length != 0x2c");
	}
	iff->table = bsi_bitmap_header;
	iff->table_len = 1;
	struct bsi_desc *desc = ptr;
	return iff_skip_FILE(iff, desc->ifp, chunk);
}

static struct wu_st bsif(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	// "BSIF" contains nothing
	if (chunk.len != 0) {
		return wuerr(wu_invalid_header, "BSIF chunk not empty");
	}
	iff->table = bsi_bitmap_header;
	iff->table_len = 1;
	struct bsi_desc *desc = ptr;
	desc->bsif = true;
	return iff_next_FILE(iff, desc->ifp, chunk);
}

static const struct iff_table bsi_init_table[] = {
	{.id = FOURCC('B', 'S', 'I', 'F'), .fn = bsif},
	{.id = FOURCC('I', 'F', 'H', 'D'), .fn = ifhd},
};

struct wu_st bsi_init(struct bsi_desc *desc, FILE *ifp) {
	/* IFHD format layout:
		Header        (IFHD)
		Bitmap header (BHDR)
		Colormap      (CMAP), optional
		Raster        (DATA)
		EOF           (END )

	 * BSIF format layout:
		Header        (BSIF)
		Bitmap header (BHDR)
		Raster        (DATA)
		EOF           (END )
	*/
	*desc = (struct bsi_desc) {.ifp = ifp};
	struct iff_state iff = {
		.table = bsi_init_table,
		.table_len = ARRAY_LEN(bsi_init_table),
		.user = desc,
		.endian = big_endian,
		.align_sh = 1,
	};
	return iff_next_FILE(&iff, ifp, (struct iff_chunk){0});
}
