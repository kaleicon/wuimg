// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/file.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/graphics_adapters.h"
#include "raster/fmt.h"

#include "lib/atari.h"

/* In the Atari ST, the bits for each 16 pixel run are stored in a planar
 * format, with each bitplane kept in a 16-bit big-endian word. The number
 * of planes/words is the same as the image bitdepth.
 *
 * If you are in Low Resolution mode, which is 320x200 with a bitdepth of 4,
 * memory looks like this:

	.--------------- Pixels 0 to 15 --------------.  .-- Pixels 16 to 31...
	+---------+ +---------+ +---------+ +---------+  +---------+ +----
	| Plane 0 | | Plane 1 | | Plane 2 | | Plane 3 |  | Plane 0 | | ...
	+---------+ +---------+ +---------+ +---------+  +---------+ +----

 * Plane 0 contains the least-significant bits of the 16-pixel group.
 * Plane 1 the second least-significant bits, and so on.
 * Higher bits correspond to the leftmost pixels. So Bit 15 of Plane 0 is the
 * lower bit of Pixel 0, Bit 14 the lower bit of Pixel 1, etc.

http://www.bitsavers.org/pdf/atari/ST/Atari_ST_GEM_Programming_1986/GEM_0904.pdf

 * Format documentation:
https://www.atari-wiki.com/index.php?title=ST_Picture_Formats
*/

static const size_t VIDEO_RAM = 32000; // In bytes

static const uint8_t ST_LOW_DEPTH = 4;
static const size_t ST_LOW_HEIGHT = 200;
static const size_t ST_LOW_WIDTH = VIDEO_RAM * (8/ST_LOW_DEPTH) / ST_LOW_HEIGHT;

static const uint8_t ST_MEDIUM_DEPTH = 2;
static const size_t ST_MEDIUM_HEIGHT = 200;
static const size_t ST_MEDIUM_WIDTH = VIDEO_RAM * (8/ST_MEDIUM_DEPTH) / ST_MEDIUM_HEIGHT;

static const uint8_t ST_HIGH_DEPTH = 1;
static const size_t ST_HIGH_HEIGHT = 400;
static const size_t ST_HIGH_WIDTH = VIDEO_RAM * (8/ST_HIGH_DEPTH) / ST_HIGH_HEIGHT;

const char * atari_st_res_str(const enum atari_st_res res) {
	switch (res) {
	case atari_st_res_low: return "Low";
	case atari_st_res_medium: return "Medium";
	case atari_st_res_high: return "High";
	}
	return "???";
}

/* Common functions */

static uint16_t ste_pal_rotate(uint16_t p) {
	/* Palette is 0000rrrrggggbbbb, but the least-significant bit of each
	 * color comes first. That is, they are in 0321 order. */
	return (uint16_t)((p & 0x7777) << 1 | (p & 0x8888) >> 3);
}

static void st_interleave(struct wuimg *img, const uint16_t *src,
const enum atari_st_res res) {
	const uint8_t planes = (res == atari_st_res_low)
		? ST_LOW_DEPTH : ST_MEDIUM_DEPTH;
	const size_t width = (res == atari_st_res_low)
		? ST_LOW_WIDTH : ST_MEDIUM_WIDTH;

	const size_t src_stride = width/(16/planes);
	for (size_t y = 0; y < img->h; ++y) {
		for (size_t group = 0; group < width/16; ++group) {
			const size_t d_base = y*width + group*16;
			const size_t s_base = y*src_stride + (group * planes);
			for (uint8_t p = 0; p < planes; ++p) {
				const size_t s = endian16b(src[s_base + p]);
				for (uint8_t bit = 0; bit < 16; ++bit) {
					img->data[d_base + bit] |= (uint8_t)(
						((s << bit) & 0x8000) >> (15 - p)
					);
				}
			}
		}
	}
}

static unsigned gfa_colormap(unsigned idx, unsigned x);
static unsigned spu_colormap(unsigned idx, unsigned x);
static void spu_like_dec(struct wuimg *img, const uint16_t *src,
const uint16_t *pal, const bool is_gfa) {
	uint16_t *dst = (uint16_t *)img->data;
	const size_t d_stride = ST_LOW_WIDTH;
	const size_t s_stride = ST_LOW_WIDTH/4;
	// Skip first row as it'll always end up black
	for (size_t y = 1; y < img->h; ++y) {
		for (unsigned group = 0; group < ST_LOW_WIDTH/16; ++group) {
			const size_t d_base = y*d_stride + group*16;
			const size_t s_base = y*s_stride + group*4;
			uint16_t planes[4];
			for (uint8_t i = 0; i < ARRAY_LEN(planes); ++i) {
				planes[i] = endian16b(src[s_base+i]);
			};
			for (unsigned bit = 0; bit < 16; ++bit) {
				unsigned idx = 0;
				for (uint8_t plane = 0; plane < ARRAY_LEN(planes); ++plane) {
					const unsigned p = planes[plane];
					idx |= (1u & (p >> (15-bit))) << plane;
				}
				const unsigned idx2 = group*16 + bit;
				idx = is_gfa
					? gfa_colormap(idx, idx2)
					: spu_colormap(idx, idx2);
				const unsigned mul = is_gfa ? 46 : 48;
				dst[d_base + bit] = pal[y*mul + idx];
			}
		}
	}
}

static struct wu_st load_raw_size(struct wuimg *img,
const enum atari_st_res res, FILE *ifp, const size_t ram_len) {
	if (res == atari_st_res_high) {
		return fmt_load_raster_st(img, ifp);
	}
	uint16_t *ram = malloc(ram_len);
	if (ram) {
		const size_t w = fread(ram, 1, ram_len, ifp);
		st_interleave(img, ram, res);
		free(ram);
		return wuerr_partial(w, ram_len);
	}
	return WUERR_HERE(wu_alloc_error);
}

static struct wu_st load_raw(struct wuimg *img, const enum atari_st_res res,
FILE *ifp) {
	return load_raw_size(img, res, ifp, VIDEO_RAM);
}

static struct wu_st set_pal(struct wuimg *img, const uint8_t src[static 32]) {
	struct palette *pal = wuimg_palette_init(img);
	if (pal) {
		const uint8_t sh = 8;
		const int scale = (0xff << sh) / 0x07 + 1;
		for (size_t i = 0; i < 16; ++i) {
			uint8_t rgb[3] = {
				src[i*2],
				src[i*2 + 1] >> 4,
				src[i*2 + 1],
			};
			for (size_t n = 0; n < sizeof(rgb); ++n) {
				rgb[n] = (uint8_t)(((rgb[n] & 7) * scale)
					>> sh);
			}
			pal->color[i] = (struct pix_rgba8) {
				.r = rgb[0],
				.g = rgb[1],
				.b = rgb[2],
				.a = 0xff,
			};
		}
		return WU_OK;
	}
	return WUERR_HERE(wu_alloc_error);
}

static struct wu_st set_dims(struct wuimg *img, const enum atari_st_res res,
const void *restrict pal) {
	img->channels = 1;
	switch (res) {
	case atari_st_res_low:
		img->w = ST_LOW_WIDTH;
		img->h = ST_LOW_HEIGHT;
		img->bitdepth = 8;
		return set_pal(img, pal);
	case atari_st_res_medium:
		img->w = ST_MEDIUM_WIDTH;
		img->h = ST_MEDIUM_HEIGHT;
		img->bitdepth = 8;
		return set_pal(img, pal);
	case atari_st_res_high:
		img->w = ST_HIGH_WIDTH;
		img->h = ST_HIGH_HEIGHT;
		img->bitdepth = 1;
		img->cs.invert = buf_endian16b(pal) & 1;
		return WU_OK;
	}
	return WUERR_HERE(wu_invalid_header);
}


/* Calamus Raster Graphic
http://fileformats.archiveteam.org/wiki/Calamus_Raster_Graphic
*/
static const size_t CRG_HEADER_SIZE = 42;

struct wu_st crg_decode(const struct wuptr mem, struct wuimg *img) {
	const size_t sixe = wuimg_size(img);
	return wuerr_partial(decomp_topbitrle(img->data, sixe,
		mem.ptr + CRG_HEADER_SIZE, mem.len - CRG_HEADER_SIZE, 1), sixe);
}

struct wu_st crg_get_info(const struct wuptr mem, struct wuimg *img) {
	/* CRG header:
		Offset  Type    Name
		0       char    ID[10]
		10      u8      ???[4]
		14      u32     FileSize  // minus 24
		18      u16     ???
		20      u32     Width
		24      u32     Height
		28      u8      ???[10]
		38      u32     StreamSize
		42      u8      RLEStream[StreamSize]
	*/
	const uint8_t magic[] = {
		'C', 'A', 'L', 'A', 'M', 'U', 'S',
		'C', 'R', 'G'
	};
	if (mem.len < CRG_HEADER_SIZE) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(mem.ptr, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}
	img->w = buf_endian32b(mem.ptr + 20);
	img->h = buf_endian32b(mem.ptr + 24);
	img->channels = 1;
	img->bitdepth = 1;
	img->cs.invert = true;
	return WU_OK;
}

/* Dali */

struct wu_st dali_decode(struct dali_desc *desc, struct wuimg *img) {
	return load_raw(img, desc->res, desc->ifp);
}

static bool dali_ext(struct dali_desc *desc, const uint8_t ext[static 3]) {
	switch (ext[2]) {
	case '0': case '1': case '2':
		desc->res = ext[2] - '0';
		return true;
	}
	return false;
}

struct wu_st dali_parse(struct dali_desc *desc, struct wuimg *img, FILE *ifp,
const uint8_t ext[static 3]) {
	/* Dali format:
		Offset  Type    Name
		0       u32     ID                 // Always 0
		4       u16     Palette[16]
		36      u8      Reserved[96]       // Often 0
		128     u16     ScreenDump[16000]
		32128
	*/

	*desc = (struct dali_desc) {
		.ifp = ifp,
	};
	if (dali_ext(desc, ext)) {
		uint32_t header[32];
		if (fread(header, sizeof(header), 1, ifp)) {
			if (!header[0]) {
				return set_dims(img, desc->res, header+1);
			}
			return WUERR_HERE(wu_invalid_signature);
		}
		return WUERR_HERE(wu_unexpected_eof);
	}
	return WUERR_HERE(wu_unknown_file_type);
}


/* DEGAS */

void degas_cleanup(struct degas_desc *desc) {
	free(desc->cycle);
}

static void load_elite_crng(struct degas_desc *desc, struct wuimg *img) {
	/* DEGAS Elite adds the following footer to normal DEGAS files:
		Offset  Type    Name
		0       u16     ColorAnimStart[4]
		8       u16     ColorAnimEnd[4]
		16      u16     AnimDirection[4]
		24      u16     AnimDelay[4]
		32
	 * One may even find High Depth files like this, for whatever reason.
	*/

	enum elite_direction {
		elite_left = 0, // high to low i guess??
		elite_off = 1,
		elite_right = 2,
	};

	const uint16_t slots = 4;
	struct palette_cycle *cycle = palette_cycle_new(slots);
	if (cycle) {
		desc->cycle = cycle;
		uint16_t buf[4][4];
		if (fread(buf, sizeof(buf), 1, desc->ifp)) {
			const unsigned max_idx = 16;
			for (size_t i = 0; i < slots; ++i) {
				uint16_t lo = endian16b(buf[0][i]);
				uint16_t hi = endian16b(buf[1][i]);
				enum elite_direction direction = endian16b(
					buf[2][i]);
				uint16_t delay = endian16b(buf[3][i]);
				cycle->crng[i] = (struct palette_crng) {
					.lo = (uint8_t)lo,
					.hi = (uint8_t)hi,
					.reverse = direction == elite_left,
					.active = !(direction & 1) && lo < hi
						&& hi < max_idx && delay < 128,
					.secs = (128 - delay) / 60.f,
				};
				cycle->active_nr += cycle->crng[i].active;
			}
			if (cycle->active_nr) {
				cycle->len = slots;
				palette_cycle_set(cycle, img->u.palette);
				img->evolving = true;
			}
		}
	}
}

static void degas_deinterleave(struct wuimg *img, const enum atari_st_res res,
const uint8_t *src) {
	const uint8_t planes = (res == atari_st_res_low)
		? ST_LOW_DEPTH : ST_MEDIUM_DEPTH;

	const size_t outstride = wuimg_stride(img);
	const size_t instride = strip_length(img->w, 1, 1) * planes;
	for (size_t y = 0; y < img->h; ++y) {
		bitplane_interleave_row8(img->data + outstride*y,
			src + instride*y, img->w, planes, 1);
	}
}

static struct wu_st degas_decomp(struct degas_desc *desc, struct wuimg *img) {
	int8_t *pb = malloc(desc->size);
	if (pb) {
		const size_t pb_len = fread(pb, 1, desc->size, desc->ifp);
		const size_t unpack_len = VIDEO_RAM << desc->paintpro;
		uint8_t *unpack;
		if (desc->res == atari_st_res_high) {
			unpack = img->data;
		} else {
			unpack = malloc(unpack_len);
			if (!unpack) {
				free(pb);
				return WUERR_HERE(wu_alloc_error);
			}
		}
		const size_t w = decomp_packbits(unpack, unpack_len, pb, pb_len);
		free(pb);
		if (unpack != img->data) {
			degas_deinterleave(img, desc->res, unpack);
			free(unpack);
			load_elite_crng(desc, img);
		}
		return wuerr_partial(w, wuimg_size(img));
	}
	return WUERR_HERE(wu_alloc_error);
}

struct wu_st degas_decode(struct degas_desc *desc, struct wuimg *img) {
	if (desc->compressed) {
		return degas_decomp(desc, img);
	}
	const struct wu_st st = load_raw_size(img, desc->res, desc->ifp,
		VIDEO_RAM << desc->paintpro);
	if (wu_isok(st) && desc->is_elite && desc->res != atari_st_res_high) {
		load_elite_crng(desc, img);
	}
	return st;
}

struct wu_st degas_parse(struct degas_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* DEGAS format:
		Offset  Type    Name
		0       u16     Flags
		2       u16     Palette[16]
		34      u16     Data[]
	 * For uncompressed files, Data is 16000 words in size, and so the file
	 *   should be 32034 bytes. If it's 32066 bytes (32034 + 32) instead,
	 *   then it's a DEGAS Elite file, and has a color animation struct
	 *   at the end.
	 * For compressed files, the color animation struct is always present,
	 *   and so the size of Data is the file size minus header and footer.
	 * PaintPro/PlusPaint files are the same, but the bitmap may be twice
	 *   the size.
	*/

	desc->cycle = NULL;
	uint16_t src[17];
	if (fread(src, sizeof(src), 1, ifp)) {
		const uint16_t flags = endian16b(src[0]);
		const bool compressed = flags & 0x8000;
		const enum atari_st_res res = flags & ~0x8000;
		switch (res) {
		case atari_st_res_low:
		case atari_st_res_medium:
		case atari_st_res_high:
			;size_t rem = file_remaining(ifp);
			const bool paintpro = rem >= 64000;
			size_t size_limit = compressed
				? VIDEO_RAM*2 : VIDEO_RAM;
			size_limit <<= paintpro;

			const size_t crng_size = 4*4*2;
			if (compressed) {
				if (rem <= crng_size) {
					return WUERR_HERE(wu_unexpected_eof);
				}
				rem -= crng_size;
			}
			*desc = (struct degas_desc) {
				.ifp = ifp,
				.res = res,
				.compressed = compressed,
				.is_elite = compressed
					|| (rem - size_limit >= crng_size),
				.paintpro = paintpro,
				.size = zumin(rem, size_limit),
			};
			struct wu_st st = set_dims(img, desc->res, src + 1);
			img->h <<= paintpro;
			return st;
		}
		return WUERR_HERE(wu_invalid_header);
	}
	return WUERR_HERE(wu_unexpected_eof);
}


/* EZ-Art Professional */

struct wu_st ez_decode(struct mparser mp, struct wuimg *img) {
	uint8_t *ram = malloc(VIDEO_RAM);
	if (ram) {
		const struct wuptr pack = mp_remaining(&mp);
		const size_t w = decomp_packbits(ram, VIDEO_RAM,
			(int8_t *)pack.ptr, pack.len);
		degas_deinterleave(img, atari_st_res_low, ram);
		free(ram);
		return wuerr_partial(w, VIDEO_RAM);
	}
	return WUERR_HERE(wu_alloc_error);
}

struct wu_st ez_parse(struct mparser *mp, struct wuimg *img,
const struct wuptr mem) {
	/* EZ-Art header:
		Offset  Type    Name
		0       u16     ID
		2       u16     Version?    // 0xc8 (200)
		4       u16     Palette[16]
		36      u16     ???[4]      // [*]
		44
	 * [*] Usually 0x000a 0x000f (0x0008|0x0000) 0x0000
	*/
	*mp = mp_wuptr(mem);
	const uint8_t *hdr = mp_slice(mp, 44);
	if (hdr) {
		const uint8_t sig[] = {'E', 'Z', 0, 0xc8};
		if (!memcmp(hdr, sig, sizeof(sig))) {
			return set_dims(img, atari_st_res_low, hdr+4);
		}
		return WUERR_HERE(wu_invalid_signature);
	}
	return WUERR_HERE(wu_unexpected_eof);
}


/* GFA Raytrace
 * Referenced from
https://github.com/th-otto/zview/blob/master/zview/plugins/gfartimg/gfartimg.c

 * Raster decompression routine
zview/plugins/gfartani/gfadepac.c

 * Sample images
zview/zview/tests/gfa_artist
*/
static unsigned gfa_colormap(unsigned idx, unsigned x) {
	/* Reference code indexes into a 320*16-bytes array (gfatable.c),
	 * but we don't like them look-up tables around here, so here's math
	 * that produces the same results. It may be slower, idk, idc. */
	switch (idx) {
	case 0: break;
	case 0xf:
		if (!x) {
			// Only case where output is 0
			return 0;
		}
		// fallthrough
	default:
		;unsigned hi = (idx >> 1) * 20;
		unsigned lo = (idx & 1);
		unsigned first_band = hi + lo*4 + 40;
		unsigned second_band = hi + lo*16 + 188;
		idx += (x > first_band)*0xfu;
		idx += (x > second_band)*0xfu;
	}
	return idx + 1; // output is at most 43
}

static void gfa_unpack(uint16_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len) {
	struct bitstrm bs;
	bitstrm_from_bytes(&bs, src, src_len);
	for (size_t i = 0; i < dst_len; ++i) {
		const uint32_t b = bitstrm_msb_peek_high25(&bs);
		uint16_t val;
		uint32_t adv;
		switch (b >> 30) {
		case 0: case 1:
			val = 0;
			adv = 1;
			break;
		case 2:
			val = 0xffff;
			adv = 2;
			break;
		case 3:
			/* bitstrm yields words in native-endian order,
			 * but we need big-endian for High-Res 1-bit data
			 * (read MSB to LSB, byte-per-byte) and for consistency
			 * with uncompressed Low-Res. */
			val = endian16b((uint16_t)(b >> 14));
			adv = 18;
			break;
		}
		dst[i] = val;
		bitstrm_seek(&bs, adv);
	}
}

static struct wu_st gfa_load_decompress(uint16_t *restrict dst,
const size_t dst_len, FILE *ifp) {
	// Worst case is two bits of overhead per every 16-bit word.
	const size_t max = strip_base(dst_len, 18);
	size_t len = zumin(max, file_remaining(ifp));
	uint8_t *pack = malloc(len);
	struct wu_st st;
	if (pack) {
		len = fread(pack, 1, len, ifp);
		if (len) {
			gfa_unpack(dst, dst_len, pack, len);
			st = WU_OK;
		} else {
			st = WUERR_HERE(wu_unexpected_eof);
		}
		free(pack);
	} else {
		st = WUERR_HERE(wu_alloc_error);
	}
	return st;
}

static struct wu_st gfa_low(const struct gfa_desc *desc, struct wuimg *img) {
	const size_t raster_size = VIDEO_RAM/desc->factor;
	const size_t pal_elems = 9200/desc->factor;

	/* For static images, the first 63 words of the palette are zeros,
	 * with the file palette appended to it.
	 * For animations it's the same, but with 47 words instead.
	 * Since each scanline is paired with a 46-word palette (and can only
	 * access 44 entries, the other two are out of reach), this means the
	 * first scanline will always be full black, regardless of its
	 * contents.
	 * (The palette stored in the file is already 9200 (46*200) words in
	 * size, so when adding the zeroed entries, the last 63 or 47 words
	 * read from file also become unaccessible. Why all this wackiness?) */
	const size_t pal_start = desc->frames ? 47 : 63;
	const size_t pal_len = pal_elems + pal_start;
	uint16_t *buf = calloc(raster_size + pal_len*sizeof(*buf), 1);
	if (!buf) {
		return WUERR_HERE(wu_alloc_error);
	}

	/* For scl, first comes the palette then the compressed raster.
	 * For sul, it's the uncompressed raster then the palette */
	uint16_t *pal = buf;
	uint16_t *src = buf + pal_len;
	if (desc->compressed) {
		fread(pal + pal_start, pal_elems, sizeof(*pal), desc->ifp);
		struct wu_st st = gfa_load_decompress(src, raster_size/2,
			desc->ifp);
		if (!wu_isok(st)) {
			return st;
		}
	} else {
		fread(src, 1, raster_size, desc->ifp);
		if (!fread(pal + pal_start, 1, pal_elems*2, desc->ifp)) {
			return WUERR_HERE(wu_unexpected_eof);
		}
	}

	// Don't convert to native-endian, img->layout takes care of that.
	for (size_t i = pal_start; i < pal_elems; ++i) {
		pal[i] = ste_pal_rotate(pal[i]);
	}

	spu_like_dec(img, src, pal, true);
	free(buf);
	return WU_OK;
}

struct wu_st gfa_decode(const struct gfa_desc *desc, struct wuimg *img,
uint8_t frame) {
	const long off = desc->frames ? (long)desc->frame[frame].off : 8;
	fseek(desc->ifp, off, SEEK_SET);
	if (desc->res == atari_st_res_high) {
		return gfa_load_decompress((uint16_t *)img->data,
			VIDEO_RAM/desc->factor/2, desc->ifp);
	}
	return gfa_low(desc, img);
}

static struct wu_st gfa_set_dims(struct gfa_desc *desc, struct wuimg *img) {
	/* TODO: Should we divide Width by Factor? All images fill the right
	 * side with black, but then again, "all" is just the 12 images linked
	 * up there. */
	if (desc->res == atari_st_res_low) {
		img->w = ST_LOW_WIDTH;
		img->h = ST_LOW_HEIGHT / desc->factor;
		img->channels = 4;
		img->bitdepth = 4;
		img->alpha = alpha_ignore;
		img->layout = pix_argb;
	} else {
		img->w = ST_HIGH_WIDTH;
		img->h = ST_HIGH_HEIGHT / desc->factor;
		img->channels = 1;
		img->bitdepth = 1;
		img->cs.invert = true;
	}
	return WU_OK;
}

static struct wu_st gfa_anim_setup(struct gfa_desc *desc, struct wuimg *img,
const uint8_t hdr[53]) {
	struct wu_st st = gfa_set_dims(desc, img);
	if (!wu_isok(st)) {
		return st;
	}
	if (!wuimg_anim_init(img, desc->frames, 100, 1000)) {
		return WUERR_HERE(wu_alloc_error);
	}

	uint32_t file_off = 53;
	const uint32_t pal_size = (hdr[2] == 'h' ? 0 : 18400)/desc->factor;
	for (uint8_t i = 0; i < desc->frames; ++i) {
		desc->frame[i].off = file_off;
		desc->frame[i].len = buf_endian32b(hdr + 13 + i*4);
		file_off += pal_size + desc->frame[i].len;
		wuimg_anim_frame_set(img, i, true);
	}
	return WU_OK;
}

struct wu_st gfa_init(struct gfa_desc *desc, struct wuimg *img, FILE *ifp) {
	/* GFA Raytrace headers:
		Offset  Type    Name
		0       u8      ID[3]         // "s[acu][hl]"
		3       u8      CRLF[2]       // "\r\n"
		5
	 * 's' stands for screen.
	 * 'u' and 'c' stand for uncompressed and compressed
	 * 'a' stands for animated (and compressed)
	 * 'l' and 'h' for low and high resolution.
	 * "suh" is unused. PI3 DEGAS files are used instead.

	 * GFA animated header continuation:
		5       u32     Frames        // Bias of -1, max is 9 (10)
		9       u32     SizeFactor    // 1, 2, 4, or 8
		13      u32     FrameSize[10] // 0 for unused entries
		53

	 * GFA compressed and uncompressed header continuation:
		5       u8      SizeFactor    // '1', '2', '4', or '8'
		6       u8      CRLF[2]
		8

	 * Dimensions are the same as Low and High Atari modes, but with
	 * Height divided by SizeFactor.
	*/
	uint8_t hdr[53];
	if (fread(hdr, sizeof(hdr), 1, ifp)) {
		const uint8_t crlf[2] = {'\r', '\n'};
		if (hdr[0] == 's' && !memcmp(hdr + 3, crlf, sizeof(crlf))
		&& (hdr[2] == 'h' || hdr[2] == 'l')) {
			*desc = (struct gfa_desc) {
				.ifp = ifp,
				.compressed = true,
				.res = hdr[2] == 'h'
					? atari_st_res_high
					: atari_st_res_low,
			};
			switch (hdr[1]) {
			case 'u':
				if (hdr[2] == 'h') {
					break;
				}
				desc->compressed = false;
				// fallthrough
			case 'c':
				if (!memcmp(hdr + 6, crlf, sizeof(crlf))) {
					switch (hdr[5]) {
					case '1': case '2': case '4': case '8':
						desc->factor = hdr[5] - '0';
						return gfa_set_dims(desc, img);
					}
				}
				break;
			case 'a':
				desc->frames = buf_endian32b(hdr + 5);
				if (desc->frames <= 9) {
					++desc->frames;
					desc->factor = buf_endian32b(hdr + 9);
					switch (desc->factor) {
					case 1: case 2: case 4: case 8:
						return gfa_anim_setup(desc, img,
							hdr);
					}
				}
			}
		}
		return WUERR_HERE(wu_invalid_header);
	}
	return WUERR_HERE(wu_unexpected_eof);
}


/* MegaPaint */

static size_t bld_rle(uint8_t *restrict dst, const size_t dst_len,
const uint8_t *restrict src, const size_t src_len) {
	size_t d = 0;
	size_t s = 0;
	while (s < src_len) {
		const uint8_t val = src[s];
		++s;
		size_t run = 1;
		switch (val) {
		case 0x00: case 0xff:
			if (s >= src_len) {
				return d;
			}
			run = src[s] + 1;
			++s;
			break;
		}
		if (d + run > dst_len) {
			break;
		}
		memset(dst + d, val, run);
		d += run;
	}
	return d;
}

struct wu_st bld_decode(struct bld_desc *desc, struct wuimg *img) {
	if (desc->compressed) {
		size_t rle_len = file_remaining(desc->ifp);
		uint8_t *rle = malloc(rle_len);
		if (rle) {
			rle_len = fread(rle, 1, rle_len, desc->ifp);
			const size_t dst_len = wuimg_size(img);
			const size_t w = bld_rle(img->data, dst_len, rle,
				rle_len);
			free(rle);
			return wuerr_partial(w, dst_len);
		}
		return WUERR_HERE(wu_alloc_error);
	}
	return fmt_load_raster_st(img, desc->ifp);
}

struct wu_st bld_parse(struct bld_desc *desc, struct wuimg *img, FILE *ifp) {
	/* MegaPaint format:
		Offset  Type    Name
		0       s16     Width  // If width negative, file is compressed
		2       s16     Height // Actual dims are the absolute value + 1
		4
	 * MegaPaint extension and header conflict with BSAVE formats.
	*/
	uint16_t buf[2];
	if (fread(buf, sizeof(buf), 1, ifp)) {
		int16_t height = (int16_t)endian16b(buf[1]);
		if (height > 0) {
			int16_t width = (int16_t)endian16b(buf[0]);
			*desc = (struct bld_desc) {
				.ifp = ifp,
				.compressed = width < 0,
			};
			img->w = (size_t)abs(width) + 1;
			img->h = (size_t)height + 1;
			img->channels = 1;
			img->bitdepth = 1;
			img->cs.invert = true;
			return WU_OK;
		}
		return WUERR_HERE(wu_invalid_header);
	}
	return WUERR_HERE(wu_unexpected_eof);
}


/* Spectrum 512 */

static unsigned spu_colormap(unsigned idx, unsigned x) {
	// A branchless equivalent to the algo given in the wiki
	unsigned hi = (idx >> 1) * 20;
	unsigned lo = (idx & 1);
	unsigned band = hi + lo*4;
	idx += (x > band)*0x10u;
	idx += (x > band + 160)*0x10u;
	return idx;
}

static uint16_t enhanced_spu(unsigned in, uint16_t pos) {
	unsigned down = in >> (4*pos);
	return (uint16_t)(
		(down & 0x7) << 2 | (down & 0x8) >> 1 | ((in >> (pos+13)) & 0x1)
	);
}

struct wu_st spu_decode(const struct spu_desc *desc, struct wuimg *img) {
	const size_t s_stride = ST_LOW_WIDTH/4;
	const size_t raster_len = 199*s_stride;
	const size_t pal_len = 199*3*16;
	const size_t bufsize = raster_len + pal_len;
	uint16_t *buf = malloc(bufsize*sizeof(*buf));
	if (!buf) {
		return WUERR_HERE(wu_alloc_error);
	}

	fseek(desc->ifp, (long)(s_stride*sizeof(*buf)) - 4, SEEK_CUR);
	if (!fread(buf, bufsize*sizeof(*buf), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint16_t *src = buf - s_stride;
	uint16_t *pal = buf + raster_len;
	if (desc->enhanced) {
		for (size_t i = 0; i < pal_len; ++i) {
			const uint16_t p = endian16b(pal[i]);
			uint16_t col = 0;
			for (uint8_t ch = 0; ch < 3; ++ch) {
				col |= (uint16_t)(enhanced_spu(p, ch) << (ch*5));
			}
			pal[i] = col;
		}
	} else {
		bool is_ste = false;
		for (size_t i = 0; i < pal_len; ++i) {
			/* Assume STe format first, ask questions later.
			 * No need to do multiple passes over the palette. */
			pal[i] = ste_pal_rotate(endian16b(pal[i]));
			is_ste |= pal[i] & 0x1111;
		}
		if (!is_ste) {
			// We've asked the question
			const uint32_t masks[3] = {0x7 << 9, 0x7 << 5, 0x7 << 1};
			img->layout = bitfield_from_mask(img->u.bitfield, masks,
				ARRAY_LEN(masks), img->bitdepth);
		}
	}

	pal -= 3*16;
	spu_like_dec(img, src, pal, false);
	free(buf);
	return WU_OK;
}

struct wu_st spu_init(struct spu_desc *desc, struct wuimg *img, FILE *ifp) {
	/* SPU format:
		uint16_t raster[200*80];
		uint16_t palettes[199*3*16];
	 * Images are 320x200, and each scanline uses a different palette.
	 * The first scanline is always black though, and even though it's
	 * included, it's corresponding palette isn't.

	 * There are three uncompressed variants:
	 * - Atari ST: 3-bit palette
	 * - Atari STe: 4-bit palette
	 * - Enhanced: 5-bit palette, first scanline begins with "5BIT"
	 * Each is backwards-compatible with previous ones by adding bits in
	 * unused parts of the palette. The only way to tell 3-bit and 4-bit
	 * variants apart is by checking these bits.
	 */
	const uint8_t sig[] = {'5', 'B', 'I', 'T'};
	uint8_t hdr[sizeof(sig)];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	*desc = (struct spu_desc) {
		.ifp = ifp,
		.enhanced = !memcmp(hdr, sig, sizeof(sig)),
	};
	img->w = ST_LOW_WIDTH;
	img->h = ST_LOW_HEIGHT;
	img->channels = 1;
	img->bitdepth = 16;
	img->alpha = alpha_ignore;
	img->layout = pix_bgra;
	if (!wuimg_bitfield_from_id(img, desc->enhanced ? 0x555 : 0x444)) {
		return WUERR_HERE(wu_alloc_error);
	}
	return WU_OK;
}

/* STAD PAC, Arabesque
 * Arabesque headers from "All Files 46.zip/MI-3/BINARY_1/STAD.PAC/PAC_A_OS.S"
https://www.mirari.fr/file/browse/506?folder=1319
*/

static void transpose_bytes(uint8_t *restrict dst, const uint8_t *restrict src) {
	const size_t w = 640/8;
	const size_t h = 400;
	for (size_t y = 0; y < h; ++y) {
		for (size_t x = 0; x < w; ++x) {
			dst[x + y*w] = src[h*x + y];
		}
	}
}

struct wu_st stad_decode(const struct stad_desc *desc, struct wuimg *img) {
	struct mparser mp = desc->mp;
	const size_t dst_len = wuimg_size(img);
	size_t d = 0;
	uint8_t *dst;
	if (desc->sig[3] == '6') {
		dst = calloc(dst_len, 1);
		if (!dst) {
			return WUERR_HERE(wu_alloc_error);
		}
	} else {
		dst = img->data;
	}

	/* For Arabesque 89a, output pointer must be decreased by 2.
	 * For 88b, pointer must be decreased by 1.
	 * For the rest, stream contains only one block so this doesn't
	 * matter. */
	const uint8_t rewind = desc->block_nr > 1 && desc->sig[4] == '9'
		? 2 : 1;
	for (uint16_t i = 0; i < desc->block_nr; ++i) {
		const struct wuptr src = mp_avail(&mp, desc->block[i]);
		if (src.len > 3) {
			d -= d >= rewind ? rewind : 0;
			const uint8_t from_header = src.ptr[0];
			const uint8_t header_val = src.ptr[1];
			const uint8_t from_stream = src.ptr[2];
			size_t s = 3;
			while (s < src.len) {
				const uint8_t flag = src.ptr[s];
				++s;
				size_t count = 1;
				uint8_t val = flag;
				if (flag == from_header) {
					if (src.len - s < 1) {
						break;
					}
					val = header_val;
					count += src.ptr[s];
					++s;
				} else if (flag == from_stream) {
					if (src.len - s < 2) {
						break;
					}
					val = src.ptr[s];
					++s;
					count += src.ptr[s];
					++s;
				}
				if (dst_len - d < count) {
					/* Plenty of PAC files want to write
					 * out of bounds. */
					count = dst_len - d;
					s = src.len;
				}
				memset(dst + d, val, count);
				d += count;
			}
		}
	}
	if (dst != img->data) {
		transpose_bytes(img->data, dst);
		free(dst);
	}
	return wuerr_partial(d, dst_len);
}

static void block_from_filesize(struct stad_desc *desc) {
	desc->block_nr = 1;
	desc->block[0] = (uint16_t)zumin(0xffff, desc->mp.len - desc->mp.pos);
}

#define TWOCC(a, b) (a << 8 | b)
struct wu_st stad_init(struct stad_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* STAD PAC header:
		Offset  Type    Name
		0       u8      Packing[4] // pM85 (horizontal) or pM86 (vertical)
		4

	 * Arabesque header:
		0       u8      ID[6]      // ESO88a, ESO88b, ESO89a
		6       u16     Width
		8       u16     Height
		10

	 * Arabesque with ID == "ESO88b":
		10      u16     BlockSizes[4]
		18

	 * Arabesque with ID == "ESO89a":
		10      u16     BlockNr
		12      u16     BlockSizes[BlockNr]

	 * All cases are followed by the compressed data.
	*/
	*desc = (struct stad_desc) {
		.mp = mp_wuptr(mem),
		.block_nr = 4,
	};
	const uint8_t *hdr = mp_slice(&desc->mp, 10);
	if (hdr) {
		img->channels = 1;
		img->bitdepth = 1;
		img->align_sh = 1;
		img->cs.invert = true;
		const uint8_t stad[3] = {'p', 'M', '8'};
		const uint8_t arabesque[4] = {'E', 'S', 'O', '8'};
		if (!memcmp(hdr, stad, sizeof(stad))) {
			memcpy(desc->sig, hdr, 4);
			switch (hdr[3]) {
			case '6':
			case '5':
				img->w = 640;
				img->h = 400;
				desc->mp.pos = 4;
				block_from_filesize(desc);
				return WU_OK;
			}
		} else if (!memcmp(hdr, arabesque, sizeof(arabesque))) {
			memcpy(desc->sig, hdr, 6);
			img->w = buf_endian16b(hdr + 6);
			img->h = buf_endian16b(hdr + 8);
			const uint16_t version = (uint16_t)(hdr[4] << 8 | hdr[5]);
			switch (version) {
			case TWOCC('9', 'a'):
				hdr = mp_slice(&desc->mp, 2);
				if (!hdr) {
					return WUERR_HERE(wu_unexpected_eof);
				}
				desc->block_nr = buf_endian16b(hdr);
				if (desc->block_nr < 1 || desc->block_nr > 4) {
					return wuerr(wu_invalid_header,
						"bad number of blocks");
				}
				// fallthrough
			case TWOCC('8', 'b'):
				hdr = mp_slice(&desc->mp, 2 * desc->block_nr);
				if (!hdr) {
					return WUERR_HERE(wu_unexpected_eof);
				}
				memcpy(desc->block, hdr, 2 * desc->block_nr);
				endian_loop16(desc->block, big_endian, desc->block_nr);
				return WU_OK;
			case TWOCC('8', 'a'):
				block_from_filesize(desc);
				return WU_OK;
			}
		}
		return WUERR_HERE(wu_unknown_file_type);
	}
	return WUERR_HERE(wu_unexpected_eof);
}


/* Tiny Stuff */

void tiny_cleanup(struct tiny_desc *desc) {
	free(desc->cycle);
}

static void tiny_unscramble(uint16_t *restrict dst, const uint16_t *restrict src) {
	const unsigned y_stride = 80;
	const unsigned set_stride = 200*20;
	for (uint8_t y = 0; y < 200; ++y) {
		for (uint8_t x = 0; x < 20; ++x) {
			for (uint8_t set = 0; set < 4; ++set) {
				dst[y*y_stride + x*4 + set] =
					src[set_stride*set + x*200 + y];
			}
		}
	}
}

static size_t tiny_rle(uint16_t *out, const int8_t *ctrl, const size_t clen,
const struct wuptr data) {
	// Not quite same as ILBM VDAT
	size_t dpos = 0, cpos = 0, opos = 0;
	while (cpos < clen) {
		const int8_t c = ctrl[cpos];
		++cpos;
		size_t run;
		bool cpy;
		if (c < 0) {
			run = (size_t)-c;
			cpy = true;
		} else if (c > 1) {
			run = (size_t)c;
			cpy = false;
		} else {
			if (cpos + 2 > clen) {
				break;
			}
			run = buf_endian16b(ctrl + cpos);
			cpos += 2;
			cpy = (bool)c;
		}

		if (opos + run > VIDEO_RAM/2) {
			break;
		}
		if (cpy) {
			if (dpos + run*2 > data.len) {
				break;
			}
			memcpy(out + opos, data.ptr + dpos, run*2);
			dpos += run*2;
		} else {
			if (dpos + 2 > data.len) {
				break;
			}
			memset16(out + opos, data.ptr + dpos, run);
			dpos += 2;
		}
		opos += run;
	}
	return opos;
}

struct wu_st tiny_decode(const struct tiny_desc *desc, struct wuimg *img) {
	struct mparser mp = desc->mp;
	const void *ctrl = mp_slice(&mp, desc->ctrl);
	struct wuptr data = mp_avail(&mp, desc->data*2);
	if (ctrl) {
		const bool high_res = desc->res == atari_st_res_high;
		uint16_t *buf = malloc(VIDEO_RAM * (high_res ? 1 : 2));
		if (buf) {
			size_t w = tiny_rle(buf, ctrl, desc->ctrl, data);
			void *un = high_res
				? img->data : (uint8_t *)buf + VIDEO_RAM;
			tiny_unscramble(un, buf);
			if (!high_res) {
				st_interleave(img, un, desc->res);
			}
			free(buf);
			return wuerr_partial(w, desc->ctrl);
		}
		return WUERR_HERE(wu_alloc_error);
	}
	return WUERR_HERE(wu_unexpected_eof);
}

struct wu_st tiny_parse(struct tiny_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* Tiny header:
		Offset  Type    Name
		0       u8      Resolution
		1

	 * If Resolution >= 3:
		1       u8      CycleRange
		2       s8      CycleSpeed
		3       u16     Iterations
		5

		+0      u16     Palette[16]
		+32     u16     ControlLen
		+34     u16     DataLen
		+36     s8      ControlBytes[ControlLen]
		...     u16     DataWords[DataLen]
	*/
	*desc = (struct tiny_desc) {
		.mp = mp_wuptr(mem),
	};

	const uint8_t *header = mp_slice(&desc->mp, 1);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint8_t *crng = NULL;
	uint8_t res = header[0];
	if (res >= 3) {
		res -= 3;
		crng = mp_slice(&desc->mp, 4);
	}
	header = mp_slice(&desc->mp, 36);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	desc->ctrl = buf_endian16b(header + 32);
	desc->data = buf_endian16b(header + 34);
	desc->res = res;
	struct wu_st st = set_dims(img, res, header);
	if (!wu_isok(st)) {
		return st;
	}
	if (crng && img->mode == image_mode_palette) {
		struct palette_cycle *cycle = palette_cycle_new(1);
		if (!cycle) {
			return WUERR_HERE(wu_alloc_error);
		}
		desc->cycle = cycle;
		desc->iters = buf_endian16b(crng + 2);
		palette_cycle_set(cycle, img->u.palette);
		const int8_t speed = (int8_t)crng[1];
		cycle->crng[0] = (struct palette_crng) {
			.lo = crng[0] >> 4,
			.hi = crng[0] & 0xf,
			.reverse = speed < 0,
			.active = speed,
			.secs = (float)abs(speed)/60.f,
		};
		cycle->len = 1;
		cycle->active_nr = (bool)speed;
		img->evolving = speed;
	}
	return st;
}
