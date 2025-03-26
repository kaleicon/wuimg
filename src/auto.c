// SPDX-License-Identifier: 0BSD
#include "misc/common.h"
#include "raster/fmt.h"
#include "auto.h"

#define AUTO_CSTR(arr) .size = (uint8_t)(sizeof(arr) - 1), .bytes = (const uint8_t *)arr

#define AUTO_READ(rdesc) .rlen = (uint8_t)(ARRAY_LEN(rdesc)), .read = rdesc

/* AVS
 * Defined in Appendix E-3 of the AVS user guide:
http://bitsavers.informatik.uni-stuttgart.de/pdf/stardent/002424-001_Rev_A_Application_Visualization_System_Users_Guide_1989.pdf
 * Data is in RGB format plus an auxiliary channel with no set interpretation.
*/
static const struct auto_read avs_read[] = {
	{'w', 4},
	{'h', 4},
};
const struct auto_desc avs_desc = {
	.channels = 4, .bitdepth = 8, .layout = pix_argb,
	.alpha = alpha_ignore,
	.endian = big_endian,
	AUTO_READ(avs_read),
};

// BRU - Degas Elite Brush
const struct auto_desc bru_desc = {
	.w = 8, .h = 8,
	.channels = 1, .bitdepth = 8,
	.used_bits = 1, .attr = pix_inverted,
};

// FARBFELD
static const struct auto_read farbfeld_read[] = {
	{auto_match, AUTO_CSTR("farbfeld")},
	{'w', 4},
	{'h', 4},
};
const struct auto_desc farbfeld_desc = {
	.channels = 4, .bitdepth = 16,
	.endian = big_endian,
	AUTO_READ(farbfeld_read),
};

// GEM View-Dither
static const struct auto_read gemview_read[] = {
	{auto_match, AUTO_CSTR("B&W256")},
	{'w', 2},
	{'h', 2},
};
const struct auto_desc gemview_desc = {
	.channels = 1, .bitdepth = 8,
	.endian = big_endian,
	AUTO_READ(gemview_read),
};

// HP Palmtop Icon
static const struct auto_read hpicon_read[] = {
	{auto_match, AUTO_CSTR("\x01\x00\x01\x00")},
	{'w', 2},
	{'h', 2},
};
const struct auto_desc hpicon_desc = {
	.channels = 1, .bitdepth = 1,
	.attr = pix_inverted, .endian = little_endian,
	AUTO_READ(hpicon_read),
};

// Nokia Logo Manager
// TODO: Report logo type, multiple images
static const struct auto_read nlm_read[] = {
	{auto_match, AUTO_CSTR("NLM \x01")},
	{auto_skip, 1}, // 0: Operator, 1: Caller, 2: Startup, 3: Picture image
	{auto_match, AUTO_CSTR("\0")}, // Number of images - 1
	{'w', 1},
	{'h', 1},
	{auto_match, AUTO_CSTR("\x01")}, // ???
};
const struct auto_desc nlm_desc = {
	.channels = 1, .bitdepth = 1,
	.attr = pix_inverted,
	AUTO_READ(nlm_read),
};

/* Atari Falcon True Color family */
// COKE
static const struct auto_read coke_read[] = {
	{auto_match, AUTO_CSTR("COKE format.")},
	{'w', 2},
	{'h', 2},
	{auto_match, AUTO_CSTR("\x00\x12")}, // Offset to raster, always 0x0012
};
const struct auto_desc coke_desc = {
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
	.endian = big_endian,
	AUTO_READ(coke_read),
};
// EggPaint
static const struct auto_read eggpaint_read[] = {
	{auto_match, AUTO_CSTR("TRUP")},
	{'w', 2},
	{'h', 2},
};
const struct auto_desc eggpaint_desc = {
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
	.endian = big_endian,
	AUTO_READ(eggpaint_read),
};
// FTC (Falcon True Color)
const struct auto_desc ftc_desc = {
	.w = 384, .h = 240,
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
};
// GodPaint
static const struct auto_read god_read[] = {
	{auto_skip, 2}, // File ID, but files have unconsistent values
	{'w', 2},
	{'h', 2},
};
const struct auto_desc god_desc = {
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
	.endian = big_endian,
	AUTO_READ(god_read),
};
// IndyPaint
static const struct auto_read indy_read[] = {
	{auto_match, AUTO_CSTR("Indy")},
	{'w', 2},
	{'h', 2},
	{auto_skip, 248}, // Must be zero
};
const struct auto_desc indy_desc = {
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
	.endian = big_endian,
	AUTO_READ(indy_read),
};
// Rembrandt
/* Program with documentation (in French):
https://no-fragments.atari.org/no_fragments_04/archive/work/gfx/remb306b.zip
 * TODO: Support comments and multiple images, once we find files that use them
*/
static const struct auto_read tcp_read[] = {
	/* Rembrandt header:
		Offset  Type    Name
		0       char    ID[8]
		8       u32     FileSize
		12      u16     HeaderSize
		14      u16     Version
		16      u16     NumPictures
		18
	 * Picture header:
		0       char    ID[4]
		4       u32     RasterSize
		8       u16     PictHeaderSize
		10      u16     Width
		12      u16     Height
		14      u16     TransparentColor
		16      u16     CrayonColor
		18      u8      Compression
		19      u8      HasPalette
		20      u8      Overscan
		21      u8      DoubleWidth
		22      u8      DoubleHeight
		23      char    Comment[175]
		198
	*/
	{auto_match, AUTO_CSTR("TRUECOLR")},
	{auto_skip, 4},
	{auto_match, AUTO_CSTR("\x00\x12\x00\x01\x00\x01PICT")},
	{auto_skip, 4},
	{auto_match, AUTO_CSTR("\x00\xc6")},
	{'w', 2},
	{'h', 2},
	{auto_skip, 0xc6 - 14},
};
const struct auto_desc tcp_desc = {
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
	.endian = big_endian,
	AUTO_READ(tcp_read),
};
// Spooky Sprites TRP
static const struct auto_read trp_read[] = {
	{auto_match, AUTO_CSTR("tru?")},
	{'w', 2},
	{'h', 2},
};
const struct auto_desc trp_desc = {
	.channels = 1, .bitdepth = 16,
	.layout = pix_bgra, .bitfield = 0x565,
	.endian = big_endian,
	AUTO_READ(trp_read),
};

/* Atari ST High Resolution */
// DA4 (PaintShop)
const struct auto_desc da4_desc = {
	.w = 640, .h = 800,
	.channels = 1, .bitdepth = 1,
	.attr = pix_inverted,
};
// DOO (Atari Doodle)
const struct auto_desc doo_desc = {
	.w = 640, .h = 400,
	.channels = 1, .bitdepth = 1,
	.attr = pix_inverted,
};

enum wu_error auto_load(struct image_file *infile, const struct auto_desc *desc) {
	struct wuimg *img = infile->sub_img;
	const enum wu_error st = wuimg_alloc(img);
	if (st == wu_ok) {
		return fmt_load_raster_swap(img, infile->ifp, desc->endian)
			? wu_ok : wu_unexpected_eof;
	}
	return st;
}

static size_t get_value(const uint8_t *buf, const size_t pos,
const uint8_t size, const enum endianness endian) {
	switch (size) {
	case 1: return buf[pos];
	case 2: return buf_endian16(buf + pos, endian);
	case 4: return buf_endian32(buf + pos, endian);
	}
	fatal_bug("get_value() in auto_init()", "Unexpected word size");
	// Fix no-return warnings
	return 0;
}

enum wu_error auto_init(struct image_file *infile, const struct wu_conf *conf,
const struct auto_desc *desc) {
	struct wuimg *img = infile->sub_img;
	img->w = desc->w;
	img->h = desc->h;
	img->channels = desc->channels;
	img->bitdepth = desc->bitdepth;
	img->used_bits = desc->used_bits;
	img->layout = desc->layout;
	img->attr = desc->attr;
	img->alpha = desc->alpha;
	if (desc->bitfield) {
		if (!wuimg_bitfield_from_id(img, desc->bitfield)) {
			return wu_alloc_error;
		}
	}

	if (desc->rlen) {
		size_t read = 0;
		for (uint8_t r = 0; r < desc->rlen; ++r) {
			read += desc->read[r].size;
		}

		uint8_t buf[256];
		if (read > sizeof(buf)) {
			fatal_bug(__func__, "Buffer is too small");
		}

		if (!fread(buf, read, 1, infile->ifp)) {
			return wu_unexpected_eof;
		}

		size_t pos = 0;
		for (uint8_t r = 0; r < desc->rlen; ++r) {
			const struct auto_read *dr = desc->read + r;
			switch (dr->dst) {
			case auto_match:
				if (memcmp(buf + pos, dr->bytes, dr->size)) {
					return wu_invalid_header;
				}
				break;
			case auto_skip:
				break;
			case auto_width:
				img->w = get_value(buf, pos, dr->size, desc->endian);
				break;
			case auto_height:
				img->h = get_value(buf, pos, dr->size, desc->endian);
				break;
			}
			pos += dr->size;
		}
	}
	return wuimg_exceeds_limit(img, conf) ? wu_exceeds_size_limit : wu_ok;
}
