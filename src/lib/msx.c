// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "misc/mem.h"
#include "raster/fmt.h"
#include "raster/graphics_adapters.h"
#include "msx.h"

/* TODO: Sprite support

 * SCx are BSAVE-style formats, which means they're a dump of the graphics card
 * memory, in this case the TMS9918 or the V9938, using the BSAVE command in
 * BASIC. Decoding these boils down to following the card's operation manual,
 * with the memory mappings used by MSX-BASIC. It's a tiny-bit like emulating
 * the MSX.

 * Note that the card's manuals and the MSX-BASIC spec use different names and
 * numbering for each graphic mode. BASIC naming is used in this file.

 * MSX2 handbook, chapter on BASIC:
https://github.com/Konamiman/MSX2-Technical-Handbook/blob/master/md/Chapter2.md

 * V9938 manual:
http://www.bitsavers.org/pdf/yamaha/Yamaha_V9938_MSX-Video_Technical_Data_Book_Aug85.pdf

 * TMS9900 manual (redundant since the V9938 is a superset of it, but w/e):
http://www.bitsavers.org/components/ti/TMS9900/TMS9918A_TMS9928A_TMS9929A_Video_Display_Processors_Data_Manual_Nov82.pdf

 * MSX-BASIC memory maps for each mode:
https://github.com/Konamiman/MSX2-Technical-Handbook/blob/master/md/Appendix5.md

 * Compressed Graph Saurus format:
https://web.archive.org/web/20200916193219/https://github.com/hex0cter/xee/issues/310#issuecomment-104523945
*/

static const size_t SCR2_W = 256;
static const size_t SCR2_H = 192;
static const size_t SCR3_W = 64;
static const size_t SCR3_H = 48;

static const size_t GEN_LEN = 0x800;
static const size_t NAME_LEN = 0x100;
static const size_t GAP_LEN = 0x500;
static const size_t COLOR_LEN = 0x800;

bool msx_mode_may_be_compressed(enum msx_screen mode) {
	return mode == msx_screen7;
}

bool msx_mode_may_have_alternate_field(enum msx_screen mode) {
	switch (mode) {
	case msx_screen6:
	case msx_screen7:
	case msx_screen10:
	case msx_screen12:
		return true;
	default: break;
	}
	return false;
}

static void set_msx_pal(struct palette *pal, const uint8_t grb[static 30],
const uint8_t depth, const bool is_yae) {
	const uint8_t max = is_yae ? 0x1f : 0x07;
	const int scale = (max << 8) / 0x07 + 1;
	for (int i = 0; i < (1 << depth); ++i) {
		const int hi = grb[i*2 + 1];
		const int lo = grb[i*2];
		// Preserve GRB order, because we can
		pal->color[i] = (struct pix_rgba8) {
			(uint8_t)((hi * scale) >> 8),
			(uint8_t)(((lo >> 4) * scale) >> 8),
			(uint8_t)(((lo & 0x07) * scale) >> 8),
			// First entry is always transparent
			i ? max : 0x00,
		};
	}
}

static bool search_msx_pal(struct palette *pal, const uint8_t *grb,
const uint8_t depth, const bool is_yae) {
	unsigned short acc = 0;
	for (int i = 0; i < (1 << depth); ++i) {
		acc |= grb[i*2 + 1] << 8 | grb[i*2];
	}
	if (acc && !(acc & 0xf888)) {
		set_msx_pal(pal, grb, depth, is_yae);
		return true;
	}
	return false;
}

static bool read_pal_file(FILE *ifp, struct palette *pal, const uint8_t depth,
const bool is_yae) {
	uint8_t grb[0x20];
	if (fread(grb, 2u << depth, 1, ifp)) {
		return search_msx_pal(pal, grb, depth, is_yae);
	}
	return false;
}

static bool read_pal_at(const struct msx_desc *desc, const long offset,
struct palette *pal, const uint8_t depth, const bool is_yae) {
	if (offset < desc->end) {
		fseek(desc->ifp, offset + 7, SEEK_SET);
		return read_pal_file(desc->ifp, pal, depth, is_yae);
	}
	return false;
}

static size_t load_or_zero(uint8_t *buf, const size_t bytes, FILE *ifp) {
	const size_t read = fread(buf, 1, bytes, ifp);
	memset(buf + read, 0, bytes - read);
	return read;
}

static void default_msx2_pal(struct palette *pal, const uint8_t depth) {
	/* The TMS9900 specifies it's default palette as YCbCr values, while
	 * the V9938 uses GRB. We take the later as the intended conversion. */
	const uint8_t grb[16*2] = {
		//RB  xG
		0x00, 0x00, // transparent
		0x00, 0x00,
		0x11, 0x06,
		0x33, 0x07,
		0x17, 0x01,
		0x27, 0x03,
		0x51, 0x01,
		0x27, 0x06,
		0x71, 0x01,
		0x73, 0x03,
		0x61, 0x06,
		0x64, 0x06,
		0x11, 0x04,
		0x65, 0x02,
		0x55, 0x05,
		0x77, 0x07,
	};
	set_msx_pal(pal, grb, depth, false);
}

static struct wu_st scr2_4_decode(const struct msx_desc *desc, struct wuimg *img) {
	/* SCREEN 2 and 4 are the same outside of the sprite mode. They display
	 * at a 256*192 resolution with 4-bit palette indices, and they do this
	 * by dividing the screen into three bands (256*64), each made up of
	 * 32*8 patterns, each 8*8 pixels in size.
	 * Patterns are specified using a pattern table, consisting of
	 * Generator, Name, and Color sections, each also divided in three
	 * internally.

		| Generator | 0x0000 - 0x1800 | 0x1800 bytes (3 * 0x800)
		| Name      | 0x1800 - 0x1b00 | 0x300 bytes (3 * 0x100)
		| Palette   | 0x1b80 - 0x1bb0 | 0x30 bytes
		| Color     | 0x2000 - 0x3800 | 0x1800 bytes (3 * 0x800)

	 * For each screen division, read a byte from the corresponding Name
	 * subsection. This is an index to an 8-byte group in the Generator
	 * and Color subsections.
	 * The bits in each Generator byte are read in MSB-to-LSB order to make
	 * an 8-pixel row. The bits select either the high (1) or low (0)
	 * nibble of the matching Color byte as the palette entry for the
	 * pixel.

	 * The 8*8 patterns are drawn left to right and top to bottom. So,
	 * by reading Name sequentially, we draw from (x0, y0) to (x7, y7),
	 * then (x8, y0) to (x15, y7), then (x16, y0) to (x23, y7), etc.
	*/

	const size_t table_len = (GEN_LEN + NAME_LEN + COLOR_LEN) * 3 + GAP_LEN;
	uint8_t *buf = malloc(table_len);
	if (!buf) {
		return WUERR_HERE(wu_alloc_error);
	}

	const size_t read = load_or_zero(buf, table_len, desc->ifp);
	const size_t min = 0x2000;
	if (read <= min) {
		free(buf);
		return WUERR_HERE(wu_unexpected_eof);
	}

	search_msx_pal(img->u.palette, buf + 0x1b80, 4, false);

	for (size_t i = 0; i < 3; ++i) {
		const uint8_t *generator = buf + i*GEN_LEN;
		const uint8_t *names = buf + 3*GEN_LEN + i*NAME_LEN;
		const uint8_t *colors = buf + 3*GEN_LEN + 3*NAME_LEN + GAP_LEN
			+ i*COLOR_LEN;
		uint8_t *dst = img->data + 32*8 * 8*8 * i;
		for (size_t y = 0; y < 8; ++y) {
			for (size_t x = 0; x < 32; ++x) {
				const uint8_t name = names[y*32 + x];
				for (size_t yp = 0; yp < 8; ++yp) {
					const uint8_t gen = generator[name*8 + yp];
					const uint8_t color = colors[name*8 + yp];
					const size_t row = (y*8 + yp)*SCR2_W;
					for (size_t xp = 0; xp < 8; ++xp) {
						dst[row + x*8 + xp] = (gen >> (7-xp)) & 1
							? (color >> 4) : (color & 0x0f);
					}
				}
			}
		}
	}
	free(buf);
	return wuerr_partial(read, table_len);
}

static struct wu_st scr3_decode(const struct msx_desc *desc, struct wuimg *img) {
	/* SCREEN 3 displays graphics at a 64*48*16 resolution, using patterns
	 * 2*2 pixels in size. This is magnified 4 times to a 256*192
	 * resolution (we don't to that, though, that's what zooming is for).

		| Generator | 0x0000 - 0x0800 | 0x800 bytes
		| Name      | 0x0800 - 0x0b00 | 0x300 bytes
		| Palette   | 0x2020 - 0x2050 | 0x30 bytes

	 * To produce each pattern, a byte from Name is used as an index into a
	 * 8-byte group in Generator. From this group the pair numbered
	 * (y_coord % 4) is taken. The first byte will contain the top pixels,
	 * and the second the bottom two. The high-nibbles are the palette
	 * indices for the left pixels, and the low-nibbles the right pixels.

		 .- - - - - - Pixel group - - - - - - -.
		+-------------------+-------------------+
		| Byte1, MSB nibble | Byte1, LSB nibble |
		+-------------------+-------------------+
		| Byte2, MSB nibble | Byte2, LSB nibble |
		+-------------------+-------------------+

	 * Apparently, some images do away with the Name section, using the X
	 * pattern coordinate plus the upper three Y coord bits as index
	 * instead.
	*/
	const bool skip_name = desc->end < 0xaff;
	const size_t table_len = GEN_LEN + (skip_name ? 0 : NAME_LEN*3);
	uint8_t *buf = malloc(table_len);
	if (!buf) {
		return WUERR_HERE(wu_alloc_error);
	}

	const size_t read = load_or_zero(buf, table_len, desc->ifp);

	const uint8_t *generator = buf;
	const uint8_t *names = buf + GEN_LEN;
	const size_t w = SCR3_W/2;
	const size_t h = SCR3_H/2;
	for (size_t y = 0; y < h; ++y) {
		for (size_t x = 0; x < w; ++x) {
			const uint8_t name = table_len > GEN_LEN
				? names[y*w + x] : (uint8_t)((y >> 2 << 5) + x);
			const uint8_t *gen = generator + name*8 + (y & 0x03)*2;
			for (size_t yp = 0; yp < 2; ++yp) {
				const size_t row = (y*2 + yp)*w;
				img->data[row + x] = gen[yp];
			}
		}
	}
	free(buf);

	read_pal_at(desc, 0x2020, img->u.palette, 4, false);
	return wuerr_partial(read, table_len);
}

static struct wu_st fread_decode(const struct msx_desc *desc, struct wuimg *img,
const long pal_offset) {
	/* SCREEN 5, 6, 7, and 8 are basically raw formats. There's only a Name
	 * section (a normal raster) and optional palette.

	 * SCREEN 5 displays at a 256*212 or 256*192 resolution with 4-bit
	 * indices. SCREEN 6 halves the indices to 2-bits and doubles the
	 * horizontal resolution to 512. Both modes therefore occupy the same
	 * space.

		| Name    | 0x0000 - 0x6000 | 192 line mode
		|         | 0x0000 - 0x6a00 | 212 line mode
		| Palette | 0x7680 - 0x76b0 |

	 * SCREEN 7 displays at a 512*212 or 512*192 resolution with 4-bit
	 * indices. SCREEN 8 halves the horizontal resolution and uses 332-GRB
	 * packing. That is, 3 high-bits of green, 3 middle-bits of red, and
	 * the 2 low-bit as blue in a single byte. Both modes occupy the same
	 * space.

		| Name    | 0x0000 - 0xc000 | 192 line mode
		|         | 0x0000 - 0xd400 | 212 line mode
		| Palette | 0xfa80 - 0xfab0 | Not used in SCREEN 8

	*/

	const struct wu_st st = fmt_load_raster_st(img, desc->ifp);
	if (img->mode == image_mode_palette) {
		read_pal_at(desc, pal_offset, img->u.palette, img->bitdepth,
			false);
	}
	return st;
}

static struct wu_st msx2p_decode(const struct msx_desc *desc, struct wuimg *img) {
	/* SCREEN 10, 11, and 12 are based on SCREEN 8, but instead of a 332
	 * packing they are encoded in the YJK colorspace. SCREEN 10 and 11
	 * are additionally in YAE mode.
	*/
	struct palette *yae = NULL;
	const size_t pal_size = (desc->mode == msx_screen12)
		? 0 : sizeof(*yae);

	const size_t bytes = img->w * img->h;
	uint8_t *buf = malloc(bytes + pal_size);
	if (!buf) {
		return WUERR_HERE(wu_alloc_error);
	}

	const size_t read = load_or_zero(buf, bytes, desc->ifp);
	if (pal_size) {
		yae = (struct palette *)(buf + bytes);
		yae->refs = 0;
		if (!read_pal_at(desc, 0xfa80, yae, 4, true)) {
			default_msx2_pal(yae, 4);
		}
	}
	v9958_ykj_to_grb((upack1555_t *)img->data, buf, bytes/4, yae);
	free(buf);
	return wuerr_partial(read, bytes);
}

static struct wu_st saurus_rle(const struct msx_desc *desc, struct wuimg *img) {
	size_t s = 0;
	size_t d = 0;
	const size_t pad = 2;
	uint8_t *src = malloc(desc->end + pad);
	if (!src) {
		return WUERR_HERE(wu_alloc_error);
	}
	const size_t read = fread(src, 1, desc->end, desc->ifp);
	memset(src + read, 0, pad);
	const size_t dims = wuimg_size(img);
	while (s < read && d < dims) {
		size_t repeat;
		uint8_t val = src[s];
		++s;
		if (val >> 4) {
			img->data[d] = val;
			++d;
		} else {
			if (val) {
				repeat = val;
				val = src[s];
				++s;
			} else {
				repeat = src[s] ? src[s] : 256;
				val = src[s+1];
				s += 2;
			}
			if (dims - d < repeat) {
				break;
			}
			memset(img->data + d, val, repeat);
			d += repeat;
		}
	}
	free(src);
	return wuerr_partial(d, dims);
}

struct wu_st msx_decode(const struct msx_desc *desc, struct wuimg *img) {
	switch (desc->mode) {
	case msx_screen2:
	case msx_screen4:
		return scr2_4_decode(desc, img);
	case msx_screen3:
		return scr3_decode(desc, img);
	case msx_screen5:
	case msx_screen6:
		return fread_decode(desc, img, 0x7680);
	case msx_screen7:
		if (desc->compressed) {
			return saurus_rle(desc, img);
		}
		// fallthrough
	case msx_screen8:
		return fread_decode(desc, img, 0xfa80);
	case msx_screen10:
	case msx_screen12:
		return msx2p_decode(desc, img);
	}
	return wuerr(wu_invalid_params, "bad screen mode on decode");
}

static bool get_external_pal(struct palette *pal, const char *name,
const uint8_t pal_depth) {
	if (!name) {
		return false;
	}
	const size_t len = strlen(name) + 1;
	char *palname = memdup(name, len);
	if (!palname) {
		return false;
	}
	char *ext = palname + len - 4;
	const char cases[][2] = {
		{'p', 'l'},
		{'P', 'L'},
	};
	for (size_t i = 0; i < ARRAY_LEN(cases); ++i) {
		memcpy(ext, cases[i], sizeof(cases[i]));
		FILE *efp = fopen(palname, "rb");
		if (efp) {
			bool ok = read_pal_file(efp, pal, pal_depth, false);
			fclose(efp);
			return ok;
		}
	}
	return false;
}

static enum msx_screen mode_from_ext(const uint8_t ext[static 3]) {
	switch (ext[2]) {
	case msx_screen2:
	case msx_screen3:
	case msx_screen4:
	case msx_screen5:
	case msx_screen6:
	case msx_screen7:
	case msx_screen8:
	case msx_screen10:
		return ext[2];
	case msx_screen12:
		if (ext[0] == 'p' && ext[1] == 'i') {
			return msx_screen8;
		}
		return ext[2];
	}
	const uint8_t grp[][4] = {
		{'g', 'r', 'p', msx_screen2},
		{'s', 'r', 's', msx_screen12},
		{'y', 'j', 'k', msx_screen12},
	};
	for (size_t i = 0; i < ARRAY_LEN(grp); ++i) {
		if (!memcmp(ext, grp[i], 3)) {
			return grp[i][3];
		}
	}
	return 0;
}

struct wu_st msx_parse(struct msx_desc *desc, struct wuimg *img, FILE *ifp,
const char *name, const uint8_t ext[static 3]) {
	/* MSX-BASIC header:
		Offset  Size    Name
		0       u8      Type         // 0xfe. 0xfd for graph-saurus rle
		1       u16     StartAddress // Must be 0
		3       u16     EndAddress   // Last address of written memory
		5       u16     RunAddress   // Must be 0
		7
	*/

	*desc = (struct msx_desc) {
		.ifp = ifp,
		.mode = mode_from_ext(ext),
		.mod = ext[1],
	};
	if (!desc->mode) {
		return wuerr(wu_unknown_file_type,
			"failed to identify MSX mode");
	}

	uint8_t buf[7];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	switch (buf[0]) {
	case 0xfe: break;
	case 0xfd: desc->compressed = true; break;
	default: return wuerr(wu_invalid_header, "bad type byte");
	}

	if (buf_endian16l(buf + 1) != 0 || buf_endian16l(buf + 5) != 0) {
		return wuerr(wu_invalid_header,
			"bad start or run addresses");
	}

	desc->end = buf_endian16l(buf + 3);
	uint16_t min = 0xffff;
	uint8_t pal_depth = 4;
	img->bitdepth = 4;
	img->channels = 1;
	img->bitrange = 3;
	img->layout = pix_grba;
	// Prefer NTSC as the transfer function is the same as sRGB
	const bool USE_NTSC = true;
	if (USE_NTSC) {
		img->cs.transfer = cicp_transfer_bt470_6_system_m;
		img->cs.primaries = cicp_primaries_bt470_6_system_m;
	} else { // PAL
		img->cs.transfer = cicp_transfer_bt470_6_system_b_g;
		img->cs.primaries = cicp_primaries_bt470_6_system_b_g;
	}
	switch (desc->mode) {
	case msx_screen2:
	case msx_screen4:
		min = 0x37ff;
		img->w = SCR2_W;
		img->h = SCR2_H;
		img->bitdepth = 8;
		break;
	case msx_screen3:
		min = 0x5ff; // NYANCAT.SC3
		img->w = SCR3_W;
		img->h = SCR3_H;
		break;
	case msx_screen5:
		min = 0x5fff;
		img->w = 256;
		img->h = (desc->end < 0x69ff) ? 192 : 212;
		break;
	case msx_screen6:
		min = 0x56ff; // Fix for IMAGE5.SC6 (0x5700)
		img->w = 512;
		img->h = (desc->end < 0x69ff) ? 192 : 212;
		img->bitdepth = 2;
		pal_depth = 2;
		break;
	case msx_screen7:
		min = 0xbfff;
		img->w = 512;
		img->h = (desc->end < 0xd3ff) ? 192 : 212;
		break;
	case msx_screen8:
		min = 0xbfff;
		img->w = 256;
		img->h = (desc->end < 0xd3ff) ? 192 : 212;
		img->bitdepth = 8;
		img->bitrange = 0;
		img->layout = pix_layout_pack(1, 2, 0, 3);
		if (!wuimg_bitfield_from_id(img, 0x332)) {
			return WUERR_HERE(wu_alloc_error);
		}
		pal_depth = 0;
		break;
	case msx_screen10:
	case msx_screen12:
		min = 0xbfff;
		img->w = 256;
		img->h = (desc->end < 0xd3ff) ? 192 : 212;
		img->bitdepth = 16;
		img->bitrange = 0;
		if (!wuimg_bitfield_from_id(img, 0x555)) {
			return WUERR_HERE(wu_alloc_error);
		}
		/* These screen modes render to a higher bitdepth, so we'll
		 * read the palette when it's needed. */
		pal_depth = 0;
		break;
	}
	if (desc->compressed) {
		// I've only seen SR7 compressed files
		if (!msx_mode_may_be_compressed(desc->mode)) {
			return wuerr(wu_samples_wanted,
				"non-SR7 compressed file");
		}
	} else if (desc->end < min) {
		return wuerr(wu_invalid_header,
			"end address and screen mode mismatch");
	}

	if (pal_depth) {
		struct palette *pal = wuimg_palette_init(img);
		if (!pal) {
			return WUERR_HERE(wu_alloc_error);
		}
		desc->external_palette = desc->mod == msx_mod_graph_saurus
			&& get_external_pal(pal, name, pal_depth);
		if (!desc->external_palette) {
			default_msx2_pal(pal, pal_depth);
		}
	}
	return WU_OK;
}


/* MSX GL5/6/7/8, and Dynamic Publisher Shape/Stamp (.stp)
 * These are uncompressed MSX rasters with arbitrary dimensions.
https://marmsx.msxall.com/projetos/sketch/stp_en.php
*/

struct wu_st msxgl_parse(struct wuimg *img, FILE *ifp, const char *name,
const uint8_t ext[static 4]) {
	/* MSX gl? header:
		Offset  Type    Name
		0       u16     Width
		2       u16     Height
		4       u8      Raster[]
	 * Raster is packed, so rows may begin in the middle of a byte.
	*/
	uint16_t hdr[2];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	img->w = endian16l(hdr[0]);
	img->h = endian16l(hdr[1]);
	img->channels = 1;
	const char stp[] = "stp";
	const bool is_stamp = !memcmp(ext, stp, sizeof(stp));
	if (is_stamp) {
		/* Like GL6, but black and white using the lower bit.
		 * Upper bit is reserved for GUI according to RECOIL. */
		img->bitdepth = 2;
		img->bitrange = 1;
		img->align_sh = align_bitpack;
		img->cs.invert = true;
		return WU_OK;
	}
	img->bitrange = 3;
	img->layout = pix_grba;
	switch (ext[2]) {
	case '5': case '7':
		img->bitdepth = 4;
		img->align_sh = align_bitpack;
		break;
	case '6':
		img->bitdepth = 2;
		img->align_sh = align_bitpack;
		break;
	case '8':
		img->bitdepth = 8;
		if (!wuimg_bitfield_from_id(img, 0x332)) {
			return WUERR_HERE(wu_alloc_error);
		}
		return WU_OK;
	}
	struct palette *pal = wuimg_palette_init(img);
	if (!pal) {
		return WUERR_HERE(wu_alloc_error);
	}
	if (!get_external_pal(pal, name, img->bitdepth)) {
		default_msx2_pal(pal, img->bitdepth);
	}
	return WU_OK;
}
