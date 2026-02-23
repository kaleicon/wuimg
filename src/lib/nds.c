// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include "misc/endian.h"
#include "misc/file.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "lib/nds.h"

/* Nintendo DS G2D formats

 * Format briefing:
 * - NSCR: An image made up of tile indexes
 * - NCGR: Tiles made up of palette indexes
 * - NCLR: Palette made up of RGB555 data

 * G2D base format:
https://wiki.dshack.org/Wiki.jsp?page=G2D%20Binary%20File%20Format
*/

struct g2d_block {
	uint32_t id;
	uint32_t size;
};

struct g2d_block_spec {
	uint32_t id;
	uint32_t min_size;
};

static const size_t NDS_TILE_DIM = 8;
/* On some NSCR files, indexes == 0 should apparently be ignored, and 1
 * substracted from the others. There seems to be no way of detecting this
 * other than going over all values beforehand. */
static const bool NSCR_CHECK_SUBSTRACT = true;

const char * nds_texfmt_str(enum nds_texfmt fmt) {
	switch (fmt) {
	case nds_texfmt_pltt16: return "PLTT16";
	case nds_texfmt_pltt256: return "PLTT256";
	}
	return "???";
}

const char * nds_scrfmt_str(enum nds_scrfmt fmt) {
	switch (fmt) {
	case nds_scrfmt_text: return "Text";
	case nds_scrfmt_affine: return "Affine";
	case nds_scrfmt_affine_ext: return "Affine EXT";
	case nds_scrfmt_pltbmp: return "PLTBMP";
	case nds_scrfmt_dcbmp: return "DCBMP";
	}
	return "???";
}

const char * nds_mapping_str(enum nds_mapping mapping) {
	switch (mapping) {
	case nds_mapping_2d: return "2D";
	case nds_mapping_1d_32k: return "1D 32K";
	case nds_mapping_1d_64k: return "1D 64K";
	case nds_mapping_1d_128k: return "1D 128K";
	case nds_mapping_1d_256k: return "1D 256K";
	}
	return "???";
}

const char * nds_charfmt_str(enum nds_charfmt fmt) {
	switch (fmt) {
	case nds_charfmt_char: return "CHAR";
	case nds_charfmt_bmp: return "BMP";
	}
	return "???";
}

const char * nds_colormode_str(enum nds_colormode color) {
	switch (color) {
	case nds_colormode_16x16: return "16x16";
	case nds_colormode_256x1: return "256x1";
	case nds_colormode_256x16: return "256x16";
	}
	return "???";
}

static uint8_t nds_texfmt_tile_depth(enum nds_texfmt fmt) {
	return fmt == nds_texfmt_pltt16 ? 4 : 8;
}

static struct wu_st g2d_block_header(struct g2d_desc *desc,
struct g2d_block *block) {
	/* Block struct:
		Offset  Type    Name
		0       u8      ID[4]
		4       u32     Size
		8       u8      Data[Size if v0.1, Size-8 if v1.0]
	*/
	const uint8_t *hdr = mp_slice(&desc->mp, 8);
	if (!hdr) {
		// redundant, but silences the compiler
		memset(block, 0, sizeof(*block));
		return WUERR_HERE(wu_unexpected_eof);
	}
	*block = (struct g2d_block) {
		.id = buf_endian32l(hdr),
		.size = buf_endian32l(hdr + 4),
	};
	if (desc->version_major == 0) {
		if (block->size < 8) {
			return wuerr(wu_invalid_header,
				"negative block size in V1.0 block");
		}
		block->size -= 8;
	}
	return WU_OK;
}

static struct wu_st g2d_block_expect(struct g2d_desc *desc,
const struct g2d_block_spec spec, const uint8_t **hdr) {
	struct g2d_block block;
	struct wu_st st = g2d_block_header(desc, &block);
	if (wu_isok(st)) {
		if (block.id != spec.id) {
			return wuerr(wu_invalid_header, "unexpected block");
		} else if (block.size < spec.min_size) {
			return wuerr(wu_invalid_header, "block too short");
		}
		*hdr = mp_slice(&desc->mp, spec.min_size);
		if (!*hdr) {
			return WUERR_HERE(wu_unexpected_eof);
		}
	}
	return st;
}

static struct wu_st g2d_init(struct g2d_desc *desc, const struct wuptr mem,
const uint32_t fourcc) {
	/* G2D base header:
		Offset  Type    Name
		0       u8      ID[4]          # reversed due to endianness
		4       u16     ByteOrderMark  # 0 or 0xfeff
		6       u16     Version        # Major << 8 | Minor
		8       u32     FileSize       # Omits block headers for v0.1
		12      u16     HeaderSize
		14      u16     Blocks
		16      struct  Block[Blocks]
	*/
	desc->mp = mp_wuptr(mem);
	const uint8_t *hdr = mp_slice(&desc->mp, 16);
	if (!hdr) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (buf_endian32l(hdr) != fourcc) {
		return wuerr(wu_invalid_signature, "signature mismatch");
	}
	switch (buf_endian16l(hdr + 4)) {
	case 0: case 0xfeff:
		desc->version_minor = hdr[6];
		desc->version_major = hdr[7];
		if (buf_endian16l(hdr + 12) == 16) {
			desc->nr_blocks = buf_endian16l(hdr + 14);
			return WU_OK;
		}
		return wuerr(wu_invalid_header, "header size != 16");
	}
	return wuerr(wu_invalid_header, "unexpected byte order mark");
}

static void casecpy(char *dst, const char *src, const size_t len,
const bool upper) {
	int (*case_fn)(int c) = upper ? toupper : tolower;
	uint8_t *d = (uint8_t *)dst;
	const uint8_t *s = (uint8_t *)src;
	for (size_t i = 0; i < len; ++i) {
		d[i] = (uint8_t)(*case_fn)(s[i]);
	}
}

static bool nds_search_sibling(const char *name, const char *new_ext,
struct wuptr *map) {
	if (!name) {
		return false;
	}
	const size_t len = strlen(name);
	const char *slash = memrchr(name, '/', len);
	const size_t base_pos = slash ? (size_t)(slash + 1 - name) : 0;
	const size_t base_len = len - base_pos;

	const size_t ext_len = strlen(new_ext);
	if (base_len < ext_len + 1 || name[len - ext_len - 1] != '.'
	|| memchr(name + len - ext_len, '.', ext_len)) {
		return false;
	}
	const size_t ext_pos = len - ext_len;

	const char dir_pre[] = "../";
	const char dir_post[] = "/";
	const size_t dir_pre_len = strlen(dir_pre);
	const size_t dir_post_len = strlen(dir_post);
	const size_t dir_len = dir_pre_len + ext_len + dir_post_len;
	char *new_name = malloc(len + dir_len + 1);
	if (!new_name) {
		return false;
	}

	int fd = -1;
	for (int i = 0; i < 2 && fd < 0; ++i) {
		memcpy(new_name, name, ext_pos);
		casecpy(new_name + ext_pos, new_ext, ext_len + 1, i);
		fd = open(new_name, O_RDONLY);
		if (fd >= 0) {
			break;
		}
		char *new_base = new_name + base_pos;
		memmove(new_base + dir_len, new_base, base_len + 1);
		memcpy(new_base, dir_pre, dir_pre_len);
		char *dir_name = new_base + dir_pre_len;
		memcpy(new_base + dir_pre_len + ext_len, dir_post,
			dir_post_len);
		for (int k = 0; k < 2 && fd < 0; ++k) {
			casecpy(dir_name, new_ext, ext_len, k);
			fd = open(new_name, O_RDONLY);
		}
	}
	free(new_name);
	if (fd >= 0) {
		bool ok = file_map_fd(map, fd);
		close(fd);
		return ok;
	}
	return false;
}

static void nds_unpack555_pal(struct palette *pal, const uint8_t *restrict src,
const size_t len) {
	struct bitfield bf;
	bitfield_from_id(&bf, 0x1555, 16);
	bitfield_unpack(&bf, pal->color, src, len);
}

static struct wu_st nds_partial_cpy(struct wuimg *img, const struct wuptr data,
const uint32_t size) {
	memcpy(img->data, data.ptr, data.len);
	return wuerr_partial(data.len, size);
}

static void nds_tilecpy(uint8_t *restrict dst, const uint8_t *restrict src,
size_t htiles, size_t ty, size_t tx, size_t nr, size_t depth) {
	for (size_t y = 0; y < NDS_TILE_DIM; ++y) {
		uint8_t *row = dst + ((ty*NDS_TILE_DIM + y)*htiles + tx)*depth;
		memcpy(row, src + (nr*NDS_TILE_DIM + y)*depth, depth);
	}
}

/* NCLR - Color palette
https://wiki.dshack.org/Wiki.jsp?page=NCLR
*/

struct wu_st nclr_into_img(const struct nclr_desc *desc, struct wuimg *img) {
	return nds_partial_cpy(img, desc->data, desc->pal_size);
}

struct wu_st nclr_img_info(struct wuimg *img) {
	img->w = 16;
	img->h = img->w;
	img->channels = 1;
	img->bitdepth = 16;
	return wuimg_bitfield_from_id(img, 0x555)
		? WU_OK
		: WUERR_HERE(wu_alloc_error);
}

struct wu_st nclr_init(struct nclr_desc *desc, const struct wuptr mem) {
	const uint32_t nclr = FOURCC('N', 'C', 'L', 'R');
	struct wu_st st = g2d_init(&desc->g2d, mem, nclr);
	if (!wu_isok(st)) {
		return st;
	}

	/* PLTT struct:
		Offset  Type    Name
		0       u32     ColorFormat
		4       u32     IsExtended
		8       u32     PalSize
		12      u32     OffsetInBlock
		16      u8      Data[]
	 * Data contains up to 256 RGB555 palette entries, even if ColorFormat
	 * is PLTT16, which is interpreted as 16 palettes with 16 colors each.
	*/
	const struct g2d_block_spec pltt_spec = {
		.id = FOURCC('P', 'L', 'T', 'T'),
		.min_size = 16,
	};
	const uint8_t *hdr;
	st = g2d_block_expect(&desc->g2d, pltt_spec, &hdr);
	if (!wu_isok(st)) {
		return st;
	}

	desc->fmt = buf_endian32l(hdr);
	switch (desc->fmt) {
	case nds_texfmt_pltt16:
	case nds_texfmt_pltt256:
		break;
	default:
		return wuerr(wu_invalid_header,
			"palette format is not PLTT16 nor PLTT256");
	}
	if (buf_endian32l(hdr + 4) != 0) {
		return wuerr(wu_unsupported_feature,
			"only simple palettes supported");
	}
	desc->pal_size = buf_endian32l(hdr + 8);
	if (!desc->pal_size) {
		return wuerr(wu_no_image_data, "0 palette entries");
	} else if (desc->pal_size > 256*2) {
		return wuerr(wu_invalid_header,
			"palette size exceeds format limit");
	}

	const uint32_t offset = buf_endian32l(hdr + 12);
	desc->data = mp_avail_at(&desc->g2d.mp,
		desc->g2d.mp.pos - pltt_spec.min_size + offset, desc->pal_size);
	return desc->data.len ? WU_OK : WUERR_HERE(wu_unexpected_eof);
}

static struct wu_st nclr_into_palette(struct nclr_desc *desc,
const struct wuptr nclr_data, struct palette *pal) {
	struct wu_st st = nclr_init(desc, nclr_data);
	if (!wu_isok(st)) {
		return st;
	}
	nds_unpack555_pal(pal, desc->data.ptr, desc->data.len/2);
	return wuerr_partial(desc->data.len, desc->pal_size);
}

/* NCGR - Character Graphics
https://wiki.dshack.org/Wiki.jsp?page=NCGR
*/
void ncgr_cleanup(struct ncgr_desc *desc) {
	palette_unref(desc->pal);
}

struct wu_st ncgr_load(const struct ncgr_desc *desc, struct wuimg *img) {
	const struct wuptr src = desc->data;
	if (desc->mapping_1d || desc->charfmt == nds_charfmt_bmp) {
		return nds_partial_cpy(img, src, desc->graphics_size);
	}
	const size_t vtiles = img->h/NDS_TILE_DIM;
	const size_t htiles = img->w/NDS_TILE_DIM;
	const uint8_t depth = nds_texfmt_tile_depth(desc->fmt);
	const size_t avail = src.len / (NDS_TILE_DIM * depth);
	for (size_t ty = 0; ty < vtiles; ++ty) {
		for (size_t tx = 0; tx < htiles; ++tx) {
			const size_t nr = ty*htiles + tx;
			if (nr == avail) {
				return wuerr_partial(nr, avail);
			}
			nds_tilecpy(img->data, src.ptr, htiles, ty, tx, nr,
				depth);
		}
	}
	return WU_OK;
}

static void ncgr_img_baseinfo(struct ncgr_desc *desc, struct wuimg *img) {
	if (desc->pal) {
		img->alpha = alpha_ignore;
		wuimg_palette_set(img, palette_ref(desc->pal));
	}
}

struct wu_st ncgr_img_info(struct ncgr_desc *desc, struct wuimg *img) {
	ncgr_img_baseinfo(desc, img);
	img->channels = 1;
	img->bitdepth = nds_texfmt_tile_depth(desc->fmt);
	img->bit = little_endian;
	if (desc->mapping_1d) {
		img->h = (desc->graphics_size - 1)/img->bitdepth + 1;
		img->w = 8;
	} else {
		img->h = desc->h*8;
		img->w = desc->w*8;
	}
	return WU_OK;
}

struct wu_st ncgr_search_nclr(struct ncgr_desc *desc, const char *name) {
	struct wuptr nclr_data;
	struct palette *pal = palette_new();
	if (!pal) {
		return wuerr(wu_alloc_error, "NCGR: failed to allocate palette");
	} else  if (!nds_search_sibling(name, "NCLR", &nclr_data)) {
		palette_unref(pal);
		return wuerr(wu_open_error, "couldn't find NCLR palette file");
	}
	desc->pal = pal;
	struct wu_st st = nclr_into_palette(&desc->nclr, nclr_data, pal);
	file_unmap(&nclr_data);
	return st;
}

struct wu_st ncgr_init(struct ncgr_desc *desc, const struct wuptr mem) {
	desc->pal = NULL;

	const uint32_t ncgr = FOURCC('N', 'C', 'G', 'R');
	struct wu_st st = g2d_init(&desc->g2d, mem, ncgr);
	if (!wu_isok(st)) {
		return st;
	}

	/* CHAR struct:
		Offset  Type    Name
		0       u16     YChars
		2       u16     XChars
		4       u32     ColorFormat
		8       u32     MappingMode
		12      u32     GraphicsType
		16      u32     CharacterFormat
		20      u32     OffsetInBlock
		24      u8      Data[]

	 * If either YChars or XChars is 0xffff, the file contains
	 * a 1D sequence of tiles.
	*/
	const struct g2d_block_spec char_spec = {
		.id = FOURCC('C', 'H', 'A', 'R'),
		.min_size = 24,
	};
	const uint8_t *hdr;
	st = g2d_block_expect(&desc->g2d, char_spec, &hdr);
	if (!wu_isok(st)) {
		return st;
	}

	desc->h = buf_endian16l(hdr);
	desc->w = buf_endian16l(hdr+2);
	desc->mapping_1d = desc->h == 0xffff || desc->w == 0xffff;
	desc->fmt = buf_endian32l(hdr + 4);
	switch (desc->fmt) {
	case nds_texfmt_pltt16:
	case nds_texfmt_pltt256:
		break;
	default:
		return wuerr(wu_invalid_header,
			"graphic format is not PLTT16 nor PLTT256");
	}

	desc->mapping = buf_endian32l(hdr + 8);
	desc->charfmt = buf_endian32l(hdr + 12) & 0xff;
	switch (desc->charfmt) {
	case nds_charfmt_char: case nds_charfmt_bmp:
		break;
	default:
		return wuerr(wu_invalid_header,
			"graphic type is not CHARACTER nor BITMAP");
	}

	desc->graphics_size = buf_endian32l(hdr + 16);
	if (!desc->mapping_1d) {
		size_t tw = nds_texfmt_tile_depth(desc->fmt);
		size_t max = (size_t)desc->h*NDS_TILE_DIM * desc->w*tw;
		desc->graphics_size = (uint32_t)zumin(desc->graphics_size, max);
	}
	const uint32_t offset = buf_endian32l(hdr + 20);
	desc->data = mp_avail_at(&desc->g2d.mp,
		desc->g2d.mp.pos - char_spec.min_size + offset,
		desc->graphics_size);
	return (desc->data.len)
		? WU_OK
		: WUERR_HERE(wu_unexpected_eof);
}

/* NSCR - Screen
https://wiki.dshack.org/Wiki.jsp?page=NSCR
*/
static const uint16_t NSCR_IDX_MASK = 0x3ff;

void nscr_cleanup(struct nscr_desc *desc) {
	ncgr_cleanup(&desc->ncgr);
	struct mparser *mp = &desc->ncgr.g2d.mp;
	if (mp->mem) {
		struct wuptr map = (struct wuptr) {
			.ptr = mp->mem,
			.len = mp->len,
		};
		file_unmap(&map);
	}
}

static void nscr_tilecpy(uint8_t *restrict dst, const uint8_t *restrict src,
size_t dst_stride, uint8_t depth, const uint16_t ctrl) {
	uint8_t unpack[64];
	if (depth == 4) {
		const unsigned pal = (ctrl >> 8) & 0xf0;
		for (size_t i = 0; i < depth*NDS_TILE_DIM; ++i) {
			uint8_t c = src[i];
			unpack[i*2] = (uint8_t)((c & 0xf) | pal);
			unpack[i*2+1] = (uint8_t)((c >> 4) | pal);
		}
		src = unpack;
	}
	for (size_t y = 0; y < NDS_TILE_DIM; ++y) {
		for (size_t x = 0; x < NDS_TILE_DIM; ++x) {
			size_t dx = ctrl & (0x1 << 10) ? 7-x : x;
			size_t dy = ctrl & (0x2 << 10) ? 7-y : y;
			dst[dy*dst_stride + dx] = src[y*NDS_TILE_DIM+x];
		}
	}
}

struct wu_st nscr_decode(const struct nscr_desc *desc, struct wuimg *img) {
	const struct wuptr idx = desc->data;
	const struct wuptr tile = desc->ncgr.data;
	const uint8_t tile_depth = nds_texfmt_tile_depth(desc->ncgr.fmt);
	const size_t tile_size = tile_depth * NDS_TILE_DIM;

	size_t dst_stride = img->w;
	size_t htiles = img->w/NDS_TILE_DIM;
	size_t vtiles = img->h/NDS_TILE_DIM;
	size_t total = vtiles * htiles;
	size_t decoded = 0;
	size_t pos = 0;
	for (size_t ty = 0; ty < vtiles && pos < idx.len; ++ty) {
		for (size_t tx = 0; tx < htiles && pos < idx.len; ++tx) {
			uint16_t ctrl = buf_endian16l(idx.ptr + pos);
			pos += 2;
			size_t off = (ctrl & NSCR_IDX_MASK)*tile_size;
			if (NSCR_CHECK_SUBSTRACT && desc->substract) {
				if (!off) {
					++decoded;
					continue;
				}
				off -= tile_size;
			}
			if (off + tile_size > tile.len) {
				continue;
			}
			nscr_tilecpy(img->data + (ty*dst_stride + tx)*NDS_TILE_DIM,
				tile.ptr + off, dst_stride, tile_depth, ctrl);
			++decoded;
		}
	}
	const char *msg = NULL;
	if (decoded*2 < pos) {
		msg = "truncated NCGR stream";
	} else if (decoded < total) {
		msg = "truncated NSCR stream";
	}
	return wuerr(decoded ? wu_ok : wu_decoding_error, msg);
}

struct wu_st nscr_init(struct nscr_desc *desc, struct wuimg *img,
const struct wuptr mem, const char *name) {
	memset(&desc->ncgr, 0, sizeof(desc->ncgr));
	const uint32_t nscr = FOURCC('N', 'S', 'C', 'R');
	struct wu_st st = g2d_init(&desc->g2d, mem, nscr);
	if (!wu_isok(st)) {
		return st;
	}

	/* SCRN struct:
		Offset  Type    Name
		0       u16     Width
		2       u16     Height
		4       u16     ColorMode
		6       u16     ScreenFormat
		8       u32     ScreenSize
		12      u8      Data[ScreenSize]
	*/
	const struct g2d_block_spec char_spec = {
		.id = FOURCC('S', 'C', 'R', 'N'),
		.min_size = 12,
	};
	const uint8_t *hdr;
	st = g2d_block_expect(&desc->g2d, char_spec, &hdr);
	if (!wu_isok(st)) {
		return st;
	}

	desc->color = buf_endian16l(hdr + 4);
	desc->fmt = buf_endian16l(hdr + 6);
	switch (desc->color) {
	case nds_colormode_16x16:
	case nds_colormode_256x1:
		break;
	case nds_colormode_256x16:
		return wuerr(wu_samples_wanted, "unsupported color mode");
	default:
		return wuerr(wu_invalid_header, "unknown color mode");
	}

	switch (desc->fmt) {
	case nds_scrfmt_text: case nds_scrfmt_affine_ext:
		break;
	case nds_scrfmt_affine:
	case nds_scrfmt_pltbmp:
	case nds_scrfmt_dcbmp:
		return wuerr(wu_samples_wanted,
			"unsupported screen format");
	default:
		return wuerr(wu_invalid_header, "unknown screen format");
	}

	img->w = buf_endian16l(hdr);
	img->h = buf_endian16l(hdr + 2);
	img->channels = 1;
	img->bitdepth = 8;
	if (img->w % NDS_TILE_DIM || img->h % NDS_TILE_DIM) {
		return wuerr(wu_uncertain_validity,
			"image dimensions not a multiple of tile size");
	}
	const size_t expect_size = sizeof(uint16_t)
		*(img->w/NDS_TILE_DIM) * (img->h/NDS_TILE_DIM);
	desc->data = mp_avail(&desc->g2d.mp, expect_size);

	struct wuptr ncgr_data;
	if (!nds_search_sibling(name, "NCGR", &ncgr_data)) {
		return wuerr(wu_open_error, "couldn't find NCGR tile file");
	}
	st = ncgr_init(&desc->ncgr, ncgr_data);
	if (!wu_isok(st)) {
		return st;
	}

	desc->substract = false;
	if (NSCR_CHECK_SUBSTRACT) {
		const uint8_t tile_depth = nds_texfmt_tile_depth(desc->ncgr.fmt);
		const size_t tile_size = tile_depth * NDS_TILE_DIM;
		const size_t nr_tiles = desc->ncgr.data.len/tile_size;
		for (size_t i = 0; i < desc->data.len/2 && !desc->substract; ++i) {
			uint16_t ctrl = buf_endian16l(desc->data.ptr + i*2);
			desc->substract = (ctrl & NSCR_IDX_MASK) == nr_tiles;
		}
	}

	struct wu_st st2 = ncgr_search_nclr(&desc->ncgr, name);
	ncgr_img_baseinfo(&desc->ncgr, img);
	return wuerr(wu_ok, st2.msg);
}


/* BGD. Used in Tsubasa Chronicle.
 * People complain about the anime but what an OST it has, oh my god. */
struct wu_st bgd_decode(const struct bgd_desc *desc, struct wuimg *img) {
	const size_t vtiles = img->h/NDS_TILE_DIM;
	const size_t htiles = img->w/NDS_TILE_DIM;
	const size_t depth = 8;
	size_t decoded = 0;
	for (size_t ty = 0; ty < vtiles; ++ty) {
		for (size_t tx = 0; tx < htiles; ++tx) {
			size_t nr = ty*htiles + tx;
			size_t idx = buf_endian16l(desc->idx + nr*2);
			if (idx >= desc->nr_tiles) {
				continue;
			}
			nds_tilecpy(img->data, desc->data, htiles, ty, tx, idx,
				depth);
			++decoded;
		}
	}
	nds_unpack555_pal(img->u.palette, desc->pal, desc->pal_entries);
	return wuerr_partial(decoded, vtiles*htiles);
}

struct wu_st bgd_init(struct bgd_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* BGD header:
		Offset  Type    Name
		0       u16     NrTiles
		2       u16     XTiles
		4       u16     YTiles
		6       u16     PalEntries
		8       u8      TileData[NrTiles][8*8]
		-       u16     TileIdx[XTiles*YTiles]
		-       u16     Palette[PalEntries]
	*/
	if (mem.len <= 8) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint8_t *hdr = mem.ptr;
	const uint16_t xtiles = buf_endian16l(hdr + 2);
	const uint16_t ytiles = buf_endian16l(hdr + 4);
	img->w = xtiles*NDS_TILE_DIM;
	img->h = ytiles*NDS_TILE_DIM;
	img->channels = 1;
	img->bitdepth = 8;
	img->alpha = alpha_ignore;

	desc->nr_tiles = buf_endian16l(hdr);
	desc->pal_entries = buf_endian16l(hdr + 6);
	if (!desc->nr_tiles) {
		return wuerr(wu_no_image_data, "no tile data");
	} else if (!desc->pal_entries || desc->pal_entries > 256) {
		return wuerr(wu_invalid_header,
			"nr of palette entries out of bounds");
	}
	const size_t data_size = desc->nr_tiles * NDS_TILE_DIM*NDS_TILE_DIM;
	const size_t idx_size = sizeof(uint16_t)*xtiles*ytiles;
	const size_t total = data_size + idx_size
		+ sizeof(uint16_t)*desc->pal_entries;
	if (mem.len - 8 < total) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	desc->data = hdr + 8;
	desc->idx = desc->data + data_size;
	desc->pal = desc->idx + idx_size;
	return wuimg_palette_init(img)
		? WU_OK
		: WUERR_HERE(wu_alloc_error);
}
