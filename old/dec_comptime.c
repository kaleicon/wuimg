// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wudefs.h"
#include "misc/common.h"
#include "dec_enable.def"

// Produces a struct definition in string form, then in code
#define EXP_STRING(exp) #exp; exp

static const char fmt_structs[] = EXP_STRING(
	struct fmt_ext {
		const char ext[6];
		const short id;
	};

	struct fmt_magic {
		const unsigned char and_mask[12];
		const unsigned char bytes[12];
		const short id;
	};
) /* EXP_STRING fmt_structs end */

// Format identifiers
enum fmt_id {
	fmt_unknown = -1,
#define WUDEC(name, _d) fmt_##name,
#include "auto.def"
#include "dec.def"
#undef WUDEC
};

// Format names
struct fmt_info {
	const char name[8];
	const char *description;
};

static const struct fmt_info info_map[] = {
#define WUDEC(name, description) { #name , description },
#include "auto.def"
#include "dec.def"
#undef WUDEC
};

const size_t AUTO_AMOUNT =
#define WUDEC(_n, _d) + 1
#include "auto.def"
#undef WUDEC
;


#if defined WU_ENABLE_RAW
static const short RAW_IF_PRESENT = fmt_raw;
#elif defined WU_ENABLE_TIFF
static const short RAW_IF_PRESENT = -1;
#endif

static struct fmt_magic magic_map[] = {
	// Auto formats
	{"\xff\xff\xff\xff" "\xff\xff\xff\xff", "farbfeld", fmt_farbfeld},
	{"\xff\xff\xff" "\xff\xff\xff", "B&W256", fmt_gemview},
	/* 0x2c and 0x20 are the image dimensions, but they're always the same
	 * so include them to make matching more robust. */
	{"\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\x01\x00\x01\x00\x2c\x00\x20\x00", fmt_hpicon},
	{"\xff\xff\xff\xff" "\xff\xfc", "NLM " "\x01\x00", fmt_nlm},

	{"\xff\xff\xff\xff" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"COKE format.", fmt_coke},
	{"\xff\xff\xff\xff", "TRUP", fmt_eggpaint},
	{"\xff\xff\xff\xff", "Indy", fmt_indy},
	{"\xff\xff\xff\xff", "tru?", fmt_trp},

#ifdef WU_ENABLE_CBG
	// Truncated
	{"\xff\xff\xff\xff" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"CompressedBG", fmt_cbg},
#endif

#ifdef WU_ENABLE_DIB
	{"\xff\xff", "BM", fmt_bmp},
#ifdef WU_ENABLE_BMZ
	{"\xff\xff\xff\xff", "ZLC3", fmt_bmz},
#endif // WU_ENABLE_BMZ
#endif // WU_ENABLE_DIB

#ifdef WU_ENABLE_DPX
	{"\xff\xff\xff\xff" "\0\0\0\0" "\xff\x00\xff\xff",
		"XPDS\0\0\0\0V\0.0", fmt_dpx},
	{"\xff\xff\xff\xff" "\0\0\0\0" "\xff\x00\xff\xff",
		"SDPX\0\0\0\0V\0.0", fmt_dpx},
#endif // WU_ENABLE_DPX

#ifdef WU_ENABLE_HG3
	{"\xff\xff\xff\xff", "HG-3", fmt_hg3},
#endif //WU_ENABLE_HG3

#ifdef WU_ENABLE_IDSP
	{"\xff\xff\xff\xff", "IDSP", fmt_idsp},
#endif //WU_ENABLE_IDSP

#ifdef WU_ENABLE_ILBM
	{"\xff\xff\xff\xff" "\0\0\0\0" "\xff\xff\xff\xff", "FORM\0\0\0\0ILBM", fmt_ilbm},
	{"\xff\xff\xff\xff" "\0\0\0\0" "\xff\xff\xff\xff", "FORM\0\0\0\0PBM ", fmt_ilbm},
#endif // WU_ENABLE_ILBM

#ifdef WU_ENABLE_MAG
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "MAKI02  ", fmt_mag},
#endif //WU_ENABLE_MAG

#ifdef WU_ENABLE_MAKI
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "MAKI01A ", fmt_maki},
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "MAKI01B ", fmt_maki},
#endif //WU_ENABLE_MAKI

#ifdef WU_ENABLE_MSX
	/* You can generally tell whether a file is an MSX-BASIC format,
	 * but you can rarely tell the screen mode it uses without the
	 * extension. */

	// Graph saurus SR5
	{"\xff\xff\xff\xff\xff\xff\xff", "\xfe\x00\x00\x00\x6a\x00\x00", fmt_msx},

#endif // WU_ENABLE_MSX

#ifdef WU_ENABLE_PCF
	{"\xff\xff\xff\xff", "\1fcp", fmt_pcf},
#endif // WU_ENABLE_PCF

#ifdef WU_ENABLE_PCX
	// Second byte is version. Valid values are 0,2,3,4,5
	{"\xff\xff\xff", "\x0a\x00\x01", fmt_pcx}, // 0
	{"\xff\xfe\xff", "\x0a\x02\x01", fmt_pcx}, // 2,3
	{"\xff\xfe\xff", "\x0a\x04\x01", fmt_pcx}, // 4,5

	{"\xff\xff\xff\xff", "\xb1\x68\xde\x3a", fmt_dcx},
#endif // WU_ENABLE_PCX

#ifdef WU_ENABLE_PDT
	{"\xff\xff\xff\xff\xfe\xff\xff\xff", "PDT10\x00\x00\x00", fmt_pdt}, // 10, 11
#endif // WU_ENABLE_PDT

#ifdef WU_ENABLE_PGX
	{"\xff\xff\xff\xff", "PGX\0", fmt_pgx},
#endif // WU_ENABLE_PGX

#ifdef WU_ENABLE_PI
	{"\xff\xff", "Pi", fmt_pi},
#endif // WU_ENABLE_PI

#ifdef WU_ENABLE_PIC
	{"\xff\xff\xff", "PIC", fmt_pic},
#endif // WU_ENABLE_PIC

#ifdef WU_ENABLE_PIC2
	{"\xff\xff\xff\xff", "P2DT", fmt_pic2},
#endif // WU_ENABLE_PIC2

#ifdef WU_ENABLE_PICTOR
	{"\xff\xff", "\x34\x12", fmt_pictor},
#endif // WU_ENABLE_PICTOR

#ifdef WU_ENABLE_PNM
	// Second byte is ASCII version.
	{"\xff\xff", "P1", fmt_pnm},
	{"\xff\xfe", "P2", fmt_pnm}, // '2', '3'
	{"\xff\xfe", "P4", fmt_pnm}, // '4', '5'
	{"\xff\xff", "P6", fmt_pnm},

	{"\xff\xff\xff", "P7\n", fmt_pnm}, // PAM
	{"\xff\xff\xff\xff\xff\xff\xff", "P7 332\n", fmt_pnm}, // Xv thumbnail
	{"\xff\xdf", "PF", fmt_pnm}, // 'F', 'f' (color/gray PFM)
	{"\xff\xdf", "PH", fmt_pnm}, // 'H', 'h' (color/gray PHM)

	{"\xff\xff\xff\xff\xff\xff", "PG ML ", fmt_pnm}, // PGX
	{"\xff\xff\xff\xff\xff\xff", "PG LM ", fmt_pnm},
#endif // WU_ENABLE_PNM

#ifdef WU_ENABLE_PRT
	{"\xff\xff\xff\xff", "PRT\0", fmt_prt},
#endif // WU_ENABLE_PRT

#ifdef WU_ENABLE_QOI
	{"\xff\xff\xff\xff", "qoif", fmt_qoi},
#endif // WU_ENABLE_QOI

#ifdef WU_ENABLE_SGI
	{"\xff\xff", "\x01\xda", fmt_sgi},
#endif // WU_ENABLE_SGI

#ifdef WU_ENABLE_SIXEL
	// {"\xff", "\x90", fmt_sixel}, // Too short
	{"\xff\xff", "\x1bP", fmt_sixel},
#endif // WU_ENABLE_SIXEL

#ifdef WU_ENABLE_SPOOKY
	{"\xff\xff\xff\xff", "tre1", fmt_tre},
	{"\xff\xff\xff\xff", "TCSF", fmt_trs},
#endif // WU_ENABLE_SPOOKY

#ifdef WU_ENABLE_SUN
	{"\xff\xff\xff\xff", "\x59\xa6\x6a\x95", fmt_sun},
#endif // WU_ENABLE_SUN

#ifdef WU_ENABLE_TIM
	{"\xff\xff\xff\xff", "\x10\x00\x00\x00", fmt_tim},
#endif // WU_ENABLE_TIM

#ifdef WU_ENABLE_TLG
	{"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff",
		"TLG5.0\x00raw\x1a", fmt_tlg},
	{"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff",
		"TLG6.0\x00raw\x1a", fmt_tlg},
#endif // WU_ENABLE_TLG

#ifdef WU_ENABLE_WGTSPR
	// Truncated due to length
	{"\xfc\xff\xff\xff" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\x00\0 Sprite Fi", fmt_wgtspr}, // 0..3
	{"\xff\xff\xff\xff" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\x04\0 Sprite Fi", fmt_wgtspr}, // 4
#endif // WU_ENABLE_WGTSPR

#ifdef WU_ENABLE_WPX
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "WPX\x1a" "BMP", fmt_wbm},
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "WPX\x1a" "IA2", fmt_wia},
#endif // WU_ENABLE_WPX

#ifdef WU_ENABLE_XBM
	{"\xff\xff", "\x2f\x2a", fmt_xbm}, // C asterisk comment in hex because
		// my editor says it will comment away half the file otherwise.
	{"\xff\xff", "\x2f\x2f", fmt_xbm}, // C slash comment in hex because
		// my editor says it will comment away the whole line otherwise.
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "#define ", fmt_xbm},
#endif // WU_ENABLE_XBM

#ifdef WU_ENABLE_XCURSOR
	{"\xff\xff\xff\xff", "Xcur", fmt_xcursor},
#endif // WU_ENABLE_XCURSOR

#ifdef WU_ENABLE_XYZ
	{"\xff\xff\xff\xff", "XYZ1", fmt_xyz},
#endif // WU_ENABLE_XYZ


#if defined WU_ENABLE_AVIF || defined WU_ENABLE_HEIF
	/* See WU_ENABLE_HEIF for notes. */
	// avic|avis
	{"\xff\xff\xff\x00" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftypavif", fmt_avif},
	{"\xff\xff\xff\x00" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftypavic", fmt_avif},
	{"\xff\xff\xff\x00" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftypavis", fmt_avif},
#endif // AVIF || HEIF

#ifdef WU_ENABLE_FLIF
	{"\xff\xff\xff\xff", "FLIF", fmt_flif},
#endif // WU_ENABLE_FLIF

#ifdef WU_ENABLE_GIF
	{"\xff\xff\xff\xff\xff\xff", "GIF87a", fmt_gif},
	{"\xff\xff\xff\xff\xff\xff", "GIF89a", fmt_gif},
#endif // WU_ENABLE_GIF

#ifdef WU_ENABLE_HEIF
	/* HEIF follows ISOBMFF, so we can't stop at 'ftyp' or try to get too
	 * clever with masks, or we could match a few hundred other formats.
	 * https://github.com/file/file/blob/master/magic/Magdir/animation

	 * The first 32-bit word (little-endian) is an offset to something not
	 * relevant to us. I've only seen values is the range 0x18-0x30, so it
	 * ought to be safe to mask out only the fourth byte. The offset must
	 * also be a multiple of 4. */

	// heic|heix|heim|heis
	{"\xff\xff\xff\x03" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftypheic", fmt_heif},
	{"\xff\xff\xff\x03" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftypheix", fmt_heif},
	{"\xff\xff\xff\x03" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftypheim", fmt_heif},
	{"\xff\xff\xff\x03" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftypheis", fmt_heif},

	// hevc|hevx|hevm|hevs
	{"\xff\xff\xff\x03" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftyphevc", fmt_heif},
	{"\xff\xff\xff\x03" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftyphevx", fmt_heif},
	{"\xff\xff\xff\x03" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftyphevm", fmt_heif},
	{"\xff\xff\xff\x03" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftyphevs", fmt_heif},

	// mif1|msf1
	{"\xff\xff\xff\x03" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftypmif1", fmt_heif},
	{"\xff\xff\xff\x03" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "ftypmsf1", fmt_heif},
#endif // WU_ENABLE_HEIF

#ifdef WU_ENABLE_JBIG2
	{"\xff\xff\xff\xff" "\xff\xff\xff\xff", "\x97JB2" "\x0d\x0a\x1a\x0a",
		fmt_jbig2},
#endif // WU_ENABLE_JBIG2

#ifdef WU_ENABLE_JPEG
	/* In a well written JPEG, the third byte would be 0xff. Not all JPEG
	 * files are well written. */
	{"\xff\xff", "\xff\xd8", fmt_jpeg},
#endif // WU_ENABLE_JPEG

#ifdef WU_ENABLE_JPEG2000
	{"\xff\xff\xff\x00" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "jP\x20\x20" "\r\n\x87\n", fmt_jp2},
	{"\xff\xff\xff\xff", "\r\n\x87\n", fmt_jp2},
	{"\xff\xff\xff\xff", "\xff\x4f\xff\x51", fmt_j2k},
#endif // WU_ENABLE_JPEG2000

#ifdef WU_ENABLE_JPEGLS
	/* JPEG-LS and JPEG share the same structure, so it's not possible to
	 * tell them apart. */
#endif // WU_ENABLE_JPEGLS

#ifdef WU_ENABLE_JPEGXL
	{"\xff\xff\xff\x00" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\0" "JXL\x20" "\r\n\x87\n", fmt_jpegxl},
	{"\xff\xff", "\xff\x0a", fmt_jpegxl},
#endif // WU_ENABLE_JPEGXL

#ifdef WU_ENABLE_LERC
	{"\xff\xff\xff\xff\xff\xff", "Lerc2 ", fmt_lerc},
	{"\xff\xff\xff\xff\xff" "\xff\xff\xff\xff\xff", "CntZImage ", fmt_lerc},
#endif // WU_ENABLE_LERC

#ifdef WU_ENABLE_PNG
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "\x89PNG\r\n\x1a\n", fmt_png},
#endif // WU_ENABLE_PNG

#ifdef WU_ENABLE_RAW
	// Olympus ORF
	{"\xff\xff\xff\xff", "IIRS", fmt_raw},
	{"\xff\xff\xff\xff", "IIRO", fmt_raw},
	{"\xff\xff\xff\xff", "MMOR", fmt_raw},

	// Panasonic RAW/RW2
	{"\xff\xff\xff\xff", "IIU\0", fmt_raw},

	// Fujifilm Raw
	/* It's actually "FUJIFILMCCD-RAW ", but until we bump up the signature
	 * length, it's truncated. */
	{"\xff\xff\xff\xff" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"FUJIFILMCCD-", fmt_raw},
#endif // WU_ENABLE_RAW

#ifdef WU_ENABLE_TIFF
	{"\xff\xff\xff\xff", "II\x2a\x00", fmt_tiff},
	{"\xff\xff\xff\xff", "MM\x00\x2a", fmt_tiff},

	// BigTIFF
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "II\x2b\x00\x08\x00\0\0", fmt_tiff},
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "MM\x00\x2b\x00\x08\0\0", fmt_tiff},
#endif // WU_ENABLE_TIFF

#ifdef WU_ENABLE_WEBP
	{"\xff\xff\xff\xff" "\0\0\0\0" "\xff\xff\xff\xff", "RIFF\0\0\0\0WEBP", fmt_webp},
#endif // WU_ENABLE_WEBP
};


/* File extensions. An enum is used where the format has no clear magic
 * sequence, or where it must be treated specially. Otherwise, we use -1 */
static struct fmt_ext ext_map[] = {
	// Auto formats
	// AVS
	{"avs", fmt_avs},
	{"mbfavs", fmt_avs},
	// BRU - Degas Brush
	{"bru", fmt_bru},
	// Farbfeld
	{"ff", -1},
	// HP Palmtop Icon
	{"icn", -1},
	// GEM View-Dither
	{"dit", -1},
	// Nokia Logo Manager
	{"nlm", -1},

	// Atari Falcon True Color family
	{"hgr", -1}, // IndyPaint
	{"tg1", -1}, // COKE
	{"trp", -1}, // EggPaint, Spooky Sprites TRU
	{"tru", -1}, // IndyPaint, Spooky Sprites TRU
	{"ftc", fmt_ftc},
	{"god", fmt_god},

	// Atari ST High Resolution
	{"da4", fmt_da4},
	{"doo", fmt_doo},

#ifdef WU_ENABLE_ATARI
	{"sd0", fmt_dali},
	{"sd1", fmt_dali},
	{"sd2", fmt_dali},

	{"pi1", fmt_degas},
	{"pi2", fmt_degas},
	{"pi3", fmt_degas},
	{"pc1", fmt_degas},
	{"pc2", fmt_degas},
	{"pc3", fmt_degas},

	{"bld", fmt_bld}, // MegaPaint

	{"tny", fmt_tiny},
	{"tn1", fmt_tiny},
	{"tn2", fmt_tiny},
	{"tn3", fmt_tiny},
	{"tn4", fmt_tiny},
	{"tn5", fmt_tiny},
	{"tn6", fmt_tiny},
#endif

#ifdef WU_ENABLE_C64
	{"gig", fmt_c64},
	{"gg", fmt_c64},
	{"koa", fmt_c64},
	{"kla", fmt_c64},
	{"ocp", fmt_c64},
#endif

#ifdef WU_ENABLE_CBG
	{"cbg", -1}, // This is just made up
#endif

#ifdef WU_ENABLE_DIB
	{"bmp", -1},
	{"bmp24", -1},
	{"cur", fmt_ico},
	{"dib", fmt_dib},
	{"ico", fmt_ico},
#ifdef WU_ENABLE_BMZ
	{"bmz", -1},
#endif // WU_ENABLE_BMZ
#endif // WU_ENABLE_DIB

#ifdef WU_ENABLE_DPX
	{"dpx", -1},
#endif

#ifdef WU_ENABLE_G00
	{"g00", fmt_g00},
#endif

#ifdef WU_ENABLE_GP4
	{"gp4", fmt_gp4},
#endif

#ifdef WU_ENABLE_HG3
	{"hg3", -1},
#endif

#ifdef WU_ENABLE_IDSP
	{"spr", -1},
	{"spr32", -1},
#endif

#ifdef WU_ENABLE_ILBM
	{"bl1", -1},
	{"iff", -1},
	{"ilbm", -1},
	{"lbm", -1},
#endif

#ifdef WU_ENABLE_MAC
	{"mac", fmt_mac},
	{"pntg", fmt_mac},
#endif

#ifdef WU_ENABLE_MAG
	{"mag", -1},
	{"max", -1},
#endif

#ifdef WU_ENABLE_MAKI
	{"mki", -1},
#endif

#ifdef WU_ENABLE_MSX
	{"sc2", fmt_msx},
	{"grp", fmt_msx},

	{"sc3", fmt_msx},

	{"sc4", fmt_msx},

	{"sc5", fmt_msx},
	{"sr5", fmt_msx}, // Graph Saurus
	{"ge5", fmt_msx},

	{"sc6", fmt_msx},
	{"s16", fmt_msx}, // Alternate field of an SC7 file
	{"sr6", fmt_msx}, // Graph Saurus

	{"sc7", fmt_msx},
	{"s17", fmt_msx}, // Alternate field of an SC7 file
	{"sr7", fmt_msx}, // Graph Saurus
	{"ge7", fmt_msx},

	{"sc8", fmt_msx},
	{"sr8", fmt_msx},
	{"ge8", fmt_msx},

	{"sca", fmt_msx},
	{"s1a", fmt_msx}, // Alternate field of an SCA file

	{"scc", fmt_msx},
	{"s1c", fmt_msx}, // Alternate field of an SCC file
	{"srs", fmt_msx}, // Graph Saurus
	{"yjk", fmt_msx},
#endif

#ifdef WU_ENABLE_PCF
	{"pcf", -1},
#endif

#ifdef WU_ENABLE_PCX
	{"dcx", -1},
	{"pcc", -1},
	{"pcx", -1},
#endif

#ifdef WU_ENABLE_PDT
	{"pdt", -1},
#endif

#ifdef WU_ENABLE_PGX
	{"pgx", -1},
#endif

#ifdef WU_ENABLE_PI
	{"pi", -1},
#endif

#ifdef WU_ENABLE_PIC2
	{"p2", -1},
#endif

#if defined WU_ENABLE_PIC || defined WU_ENABLE_PICTOR
	{"pic", -1},
#endif

#ifdef WU_ENABLE_PNM
	{"mtv", fmt_pnm},
	{"pbm", -1},
	{"pgm", -1},
	{"ppm", -1},
	{"pam", -1},
	{"pnm", -1},
	{"pfm", -1},
	{"phm", -1},
	{"p7", -1},
	{"pgx", -1},
#endif

#ifdef WU_ENABLE_PRT
	{"cps", -1},
	{"prt", -1},
#endif

#ifdef WU_ENABLE_PX
	{"px", fmt_px},
#endif

#ifdef WU_ENABLE_Q4
	{"q4", fmt_q4},
#endif

#ifdef WU_ENABLE_QOI
	{"qoi", -1},
#endif

#ifdef WU_ENABLE_SGI
	{"bw", -1},
	{"rgb", -1},
	{"rgba", -1},
	{"sgi", -1},
#endif

#ifdef WU_ENABLE_SIXEL
	{"six", fmt_sixel},
	{"sixel", fmt_sixel},
#endif

#ifdef WU_ENABLE_SPOOKY
	{"tre", -1},
	{"trs", -1},
#endif

#ifdef WU_ENABLE_SUN
	{"im1", -1},
	{"im4", -1},
	{"im8", -1},
	{"im24", -1},
	{"im32", -1},
	{"ras", -1},
	{"sun", -1},
#endif

#ifdef WU_ENABLE_TGA
	// TGA sometimes has a signature at the end. It depends on the version.
	{"tga", fmt_tga},
#endif

#ifdef WU_ENABLE_TIM
	{"tim", fmt_tim},
#endif

#ifdef WU_ENABLE_TLG
	{"tlg", -1},
#endif

#ifdef WU_ENABLE_WGTSPR
	{"spr", -1},
#endif

#ifdef WU_ENABLE_WBMP
	{"wbmp", fmt_wbmp},
#endif

#ifdef WU_ENABLE_WPX
	{"wbm", -1},
	{"wia", -1},
#endif

#ifdef WU_ENABLE_XBM
	{"xbm", fmt_xbm},
#endif

#ifdef WU_ENABLE_XWD
	{"dmp", fmt_xwd},
	{"xwd", fmt_xwd},
#endif

#ifdef WU_ENABLE_XYZ
	{"xyz", -1},
#endif


#if defined WU_ENABLE_AVIF || defined WU_ENABLE_HEIF
	{"avif", -1},
	{"avifs", -1},
#endif

#ifdef WU_ENABLE_FLIF
	{"flif", -1},
#endif

#ifdef WU_ENABLE_GIF
	{"gif", -1},
	{"gif87", -1},
	{"gif89", -1},
#endif

#ifdef WU_ENABLE_HEIF
	{"heic", -1},
	{"heics", -1},
	{"heif", -1},
	{"heifs", -1},
	{"hif", -1},
#endif

#ifdef WU_ENABLE_JBIG
	{"bie", fmt_jbig},
	{"jbg", fmt_jbig},
	{"jbig", fmt_jbig},
#endif

#ifdef WU_ENABLE_JBIG2
	{"jb2", -1},
#endif

#ifdef WU_ENABLE_JPEG
	{"dt2", -1}, // Microsoft Messenger
	{"jfi", -1},
	{"jfif", -1},
	{"jif", -1},
	{"jpe", -1},
	{"jpeg", -1},
	{"jpg", -1},
	{"jps", -1},
	{"mpo", -1},
	{"thm", -1},
#ifndef WU_ENABLE_ATARI
	// Not sure where I got this one from. Conflicts with Tiny Stuff.
	{"tn3", -1},
#endif // !WU_ENABLE_ATARI
#endif // WU_ENABLE_JPEG

#ifdef WU_ENABLE_JPEG2000
	{"j2c", -1},
	{"j2k", -1},
	{"jp2", -1},
	{"jpc", -1},
	// High throughput
	{"jph", -1},
	{"jhc", -1},
#endif

#ifdef WU_ENABLE_JPEGLS
	{"jls", fmt_jpegls},
#endif

#ifdef WU_ENABLE_JPEGXL
	{"jxl", -1},
#endif

#ifdef WU_ENABLE_LERC
	{"lrc", -1},
	{"lerc", -1},
	{"lerc1", -1},
	{"lerc2", -1},
#endif

#ifdef WU_ENABLE_PNG
	{"png", -1},
#endif

#ifdef WU_ENABLE_RAW
	{"orf", -1},
	{"raw", -1},
	{"raf", -1},
	{"rw2", -1},
	{"rwl", -1},
#endif

#if defined WU_ENABLE_RAW || defined WU_ENABLE_TIFF
	/* These RAW formats are just TIFF with extra data, and it's usually
	 * possible to show a thumbnail if libraw is not used. */
	{"arw", RAW_IF_PRESENT},
	{"cr2", RAW_IF_PRESENT},
	{"dcr", RAW_IF_PRESENT},
	{"dng", RAW_IF_PRESENT},
	{"erf", RAW_IF_PRESENT},
	{"k25", RAW_IF_PRESENT},
	{"kdc", RAW_IF_PRESENT},
	{"nef", RAW_IF_PRESENT},
	{"nrw", RAW_IF_PRESENT},
	{"pef", RAW_IF_PRESENT},
#endif

#ifdef WU_ENABLE_SVG
	{"svg", fmt_svg},
	{"svgz", fmt_svg},
#endif

#ifdef WU_ENABLE_TIFF
	{"tif", -1},
	{"tiff", -1},
#endif

#ifdef WU_ENABLE_WEBP
	{"webp", -1},
#endif
};

/* Mime types. To be used in .desktop files, someday in the future... */
static const char *mime_image_map[] = {
#ifdef WU_ENABLE_DIB
	"bmp", "x-bmp",
	"x-ms-bmp", // DIB
	"vnd.microsoft.icon", "x-icon",
#endif

#ifdef WU_ENABLE_PCX
	"x-pcx",
	"x-dcx",
#endif

#ifdef WU_ENABLE_PNM
	"x-portable-bitmap",       // PBM
	"x-portable-graymap",      // Text PGM
	"x-portable-greymap",      // Raw PGM. blame `file' for the spellings
	"x-portable-pixmap",       // PPM
	"x-portable-arbitrarymap", // PAM
	"x-xv-thumbnail",          // XV
#endif

#ifdef WU_ENABLE_TGA
	"x-tga",
#endif

#ifdef WU_ENABLE_TIM
	"x-sony-tim",
#endif

#ifdef WU_ENABLE_WBMP
	"vnd.wap.wbmp",
#endif

#ifdef WU_ENABLE_XBM
	"xbm",
#endif


#if defined WU_ENABLE_AVIF || defined WU_ENABLE_HEIF
	"avif",
#endif

#ifdef WU_ENABLE_GIF
	"gif",
#endif

#ifdef WU_ENABLE_HEIF
	"heic",
	"heif",
#endif

#ifdef WU_ENABLE_JBIG
	"jbig",
#endif

#ifdef WU_ENABLE_JPEG
	"jpeg",
#endif

#ifdef WU_ENABLE_JPEG2000
	"jp2",
#endif

#ifdef WU_ENABLE_JPEGXL
	"jxl",
#endif

#ifdef WU_ENABLE_PNG
	"png",
#endif

#ifdef WU_ENABLE_RAW
	"x-canon-cr2",
	"x-canon-crw",
	"x-fuji-raf",
	"x-olympus-orf",
#endif

#ifdef WU_ENABLE_SVG
	"svg+xml",
#endif

#ifdef WU_ENABLE_TIFF
	"tiff",
#endif

#ifdef WU_ENABLE_WEBP
	"webp",
#endif

	NULL, // Silence pedantic warnings
};

static const char *mime_application_map[] = {
	"gzip",
	"x-7z-compressed",
	"x-cpio",
	"x-lzh-compressed",
	"x-rar",
	"x-tar",
	"zip",
	NULL, // Silence pedantic warnings
};

static int magic_bits(const struct fmt_magic *magic) {
	int bits = 0;
	for (size_t i = 0; i < sizeof(magic->and_mask); ++i) {
		for (int b = 0; b < 8; ++b) {
			bits += (magic->and_mask[i] >> b) & 1;
		}
	}
	return bits;
}

static int quine_fmaskmagiccmp(const void *restrict m1, const void *restrict m2) {
	const struct fmt_magic *restrict magic1 = m1;
	const struct fmt_magic *restrict magic2 = m2;
	// Sort by number of mask bits, then by ASCII order
	int diff = magic_bits(magic2) - magic_bits(magic1);
	for (size_t i = 0; !diff && i < sizeof(magic1->bytes); ++i) {
		diff = (magic1->bytes[i] & magic1->and_mask[i])
			- (magic2->bytes[i] & magic2->and_mask[i]);
	}
	return diff;
}

static int quine_fextcmp(const void *restrict e1, const void *restrict e2) {
	const struct fmt_ext *restrict ext1 = e1;
	const struct fmt_ext *restrict ext2 = e2;
	return memcmp(ext1->ext, ext2->ext, sizeof(ext2->ext));
}

static void print_map_def(const char *name) {
	fprintf(stdout, "static const struct fmt_%s %s_map[] = {", name, name);
}

static size_t print_hex(const void *str, size_t len) {
	const unsigned char *bytes = str;
	while (len && !bytes[len - 1]) {
		--len;
	}
	fputc('"', stdout);
	for (size_t k = 0; k < len; ++k) {
		fprintf(stdout, "\\x%hhx", bytes[k]);
	}
	fputc('"', stdout);
	return len;
}

static void print_include(const char *file) {
	fprintf(stdout, "#include %s\n", file);
}

static int print_fmt_ext(size_t *min_len, size_t *max_len) {
	qsort(ext_map, ARRAY_LEN(ext_map), sizeof(*ext_map), quine_fextcmp);

	print_map_def("ext");
	for (size_t i = 0; i < ARRAY_LEN(ext_map); ++i) {
		if (i && !memcmp(ext_map[i].ext, ext_map[i-1].ext, sizeof(ext_map->ext))) {
			if (ext_map[i].id != -1 || ext_map[i-1].id != -1) {
				fputs("Conflicting extensions", stderr);
				return 1;
			}
			continue;
		}
		fputs("{{", stdout);
		const size_t outlen =  print_hex(ext_map[i].ext,
			sizeof(ext_map->ext));
		fprintf(stdout, "},%d},", ext_map[i].id);

		if (outlen < *min_len) {
			*min_len = outlen;
		}
		if (outlen > *max_len) {
			*max_len = outlen;
		}
	}
	fputs("};", stdout);
	return 0;
}

static int print_fmt_magic(size_t *min_len, size_t *max_len) {
	qsort(magic_map, ARRAY_LEN(magic_map), sizeof(*magic_map),
		quine_fmaskmagiccmp);

	print_map_def("magic");
	for (size_t i = 0; i < ARRAY_LEN(magic_map); ++i) {
		if (i && !memcmp(magic_map + i, magic_map + i-1, sizeof(*magic_map))) {
			fputs("Conflicting magic sequences", stderr);
			return 1;
		}
		fputs("{.and_mask = {", stdout);
		const size_t bytes_len = sizeof(magic_map->bytes);
		const size_t outlen = print_hex(magic_map[i].and_mask,
			bytes_len);

		fputs("}, .bytes = {", stdout);
		print_hex(magic_map[i].bytes, bytes_len);
		fprintf(stdout, "}, .id = %d},", magic_map[i].id);

		if (outlen < *min_len) {
			*min_len = outlen;
		}
		if (outlen > *max_len) {
			*max_len = outlen;
		}
	}
	fputs("};", stdout);
	return 0;
}

static void print_fmt_desc(void) {
	print_map_def("desc");
	for (size_t i = 0; i < ARRAY_LEN(info_map); ++i) {
		fputs("{.name = {", stdout);
		const int outlen = (int)print_hex(info_map[i].name,
			sizeof(info_map->name));
		fputs("},", stdout);

		// FIXME: Escape quotes
		fprintf(stdout, ".description = \"%s\",", info_map[i].description);

		fprintf(stdout, ".is_auto = %s,", i < AUTO_AMOUNT ? "true" : "false");

		const char *suf = i < AUTO_AMOUNT ? "desc" : "fn";
		fprintf(stdout, ".dec.%s = &%.*s_%s",
			suf, outlen, info_map[i].name, suf);

		fputs("},", stdout);
	}
	fputs("};", stdout);
}

static int mapsort(void) {
	/* Output of dec_headers() */
	print_include("\"dec_fn.h\"");

	/* Struct maps definition string */
	fwrite(fmt_structs, 1, sizeof(fmt_structs) - 1, stdout);

	/* The maps proper */
	print_fmt_desc();

	size_t min_mag_len = SIZE_MAX;
	size_t max_mag_len = 0;
	int err = print_fmt_magic(&min_mag_len, &max_mag_len);
	if (err) {
		return err;
	}

	size_t min_ext_len = SIZE_MAX;
	size_t max_ext_len = 0;
	err = print_fmt_ext(&min_ext_len, &max_ext_len);
	if (err) {
		return err;
	}

	fprintf(stdout,
		"static const size_t MIN_MAG_LEN = %zu;"
		"static const size_t MAX_MAG_LEN = %zu;"
		"static const size_t MIN_EXT_LEN = %zu;"
		"static const size_t MAX_EXT_LEN = %zu;",
		min_mag_len, max_mag_len,
		min_ext_len, max_ext_len);

	/* Include the rest of the file */
	fputs("\n#include \"fmtmap.c\"\n", stdout);
	return 0;
}

static int dec_headers(void) {
	const size_t name_len = sizeof(info_map->name);
	print_include("\"wudefs.h\"");
	for (size_t i = 0; i < ARRAY_LEN(info_map); ++i) {
		const char *suf = i < AUTO_AMOUNT ? "desc" : "fn";
		const char *type = i < AUTO_AMOUNT ? "auto_desc" : "image_fn";
		printf("extern const struct %s %.*s_%s;",
			type, (int)name_len, info_map[i].name, suf);
	}
	fputc('\n', stdout);
	return 0;
}

static void print_mimes(const char *type, const char **subtypes) {
	for (size_t i = 0; subtypes[i]; ++i) {
		printf("%s/%s\n", type, subtypes[i]);
	}
}

static int mimes(void) {
	print_mimes("image", mime_image_map);
	print_mimes("application", mime_application_map);
	fputs("inode/directory", stdout);
	return 0;
}

int main(const int argc, const char *argv[]) {
	if (argc > 1) {
		if (!strcmp(argv[1], "-m")) {
			return mapsort();
		} else if (!strcmp(argv[1], "-h")) {
			return dec_headers();
		} else if (!strcmp(argv[1], "-i")) {
			return mimes();
		}
	}
	return 1;
}
