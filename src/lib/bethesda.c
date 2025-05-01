// SPDX-License-Identifier: 0BSD
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
	if (wuimg_alloc_noverify(img)) {
		const size_t size = wuimg_size(img);
		switch (desc->compression) {
		case gxa_none:
			return wuerr_partial(fmt_load_raster(img, desc->ifp),
				size);
		case gxa_rle:
			;uint8_t *rle = malloc(desc->len);
			if (rle) {
				const size_t r = decomp_topbitrle(img->data,
					size, rle,
					fread(rle, 1, desc->len, desc->ifp), 1);
				free(rle);
				return wuerr_partial(r, size);
			}
			return WUERR_HERE(wu_alloc_error);
		case gxa_mystery2: break;
		}
		return wuerr(wu_unsupported_feature, NULL);
	}
	return WUERR_HERE(wu_alloc_error);
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

	img->w = endian16(info[1], little_endian);
	img->h = endian16(info[2], little_endian);
	desc->compression = endian16(info[5], little_endian);
	switch (desc->compression) {
	case gxa_none: break;
	case gxa_rle:
		;uint32_t len;
		if (!fread(&len, sizeof(len), 1, desc->ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		desc->len = endian32(len, little_endian);
		if (desc->len/2 > img->w*img->h) {
			desc->len = (uint32_t)(img->w*img->h*2);
		}
		break;
	case gxa_mystery2:
		return wuerr(wu_unsupported_feature,
			"unknown compression method 2");
	default:
		return wuerr(wu_uncertain_validity,
			"unknown compression method > 2");
	}
	img->channels = 1;
	img->bitdepth = 8;
	wuimg_palette_set(img, palette_ref(desc->pal));
	return wuimg_verify_st(img);
}

static struct wu_st load_pal(struct palette **pal, FILE *ifp,
const uint32_t chunk_len) {
	if (chunk_len != 0x300) {
		return wuerr(wu_invalid_header, "palette length != 256*3");
	}
	*pal = palette_new();
	return *pal
		? fmt_load_pal_bitrange(*pal, fmt_pal_rgb, 256, ifp, 6)
		: WUERR_HERE(wu_alloc_error);
}

static struct wu_st bbmp(struct iff_state *iff, void *ptr,
struct iff_chunk chunk) {
	(void)iff; (void)ptr; (void)chunk;
	// Finish parsing.
	return wuok();
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
		return wuerr(wu_invalid_header, "unexpected BMHD length");
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
	desc->nb_images = endian16(nb, little_endian);

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
		Bitmap (BBPM)
		EOF    (END )
	*/
	desc->ifp = ifp;
	desc->pal = NULL;

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
		uint32_t off = endian32(tab[y], little_endian);
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
	if (wuimg_alloc_noverify(img)) {
		switch (desc->compression) {
		case bsi_none:
			fseek(desc->ifp, desc->pos + desc->w*desc->h*i, SEEK_SET);
			return wuerr_partial(fmt_load_raster(img, desc->ifp),
				wuimg_size(img));
		case bsi_scanlines:
			return load_scanlines(desc, img, i);
		}
		return WUERR_HERE(wu_unsupported_feature);
	}
	return WUERR_HERE(wu_alloc_error);
}

struct wu_st bsi_set_image(struct bsi_desc *desc, struct wuimg *img) {
	img->w = desc->w;
	img->h = desc->h;
	img->channels = 1;
	img->bitdepth = 8;
	if (desc->pal) {
		wuimg_palette_set(img, palette_ref(desc->pal));
	}
	return wuimg_verify_st(img);
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
	return wuok();
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

	const enum bsi_compression compression = endian16(buf[12], little_endian);
	switch (compression) {
	case bsi_none: case bsi_scanlines:
		desc->w = endian16(buf[2], little_endian);
		desc->h = endian16(buf[3], little_endian);
		desc->nb_images = endian16(buf[7], little_endian);
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
