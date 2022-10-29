#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "dec_enable.def"

struct fmt_fn_desc {
	const char name[8];
	const bool has_callback;
};

// Produces a struct definition in string form, then in code
#define EXP_STRING(exp) #exp; exp

static const char fmt_structs[] = "struct fmt_fn {"
	"const char name[8];"
	"const fmt_dec_t dec;"
	"const fmt_callback_t callback;"
"};" EXP_STRING(
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

enum fmt_id {
	fmt_unknown = -1,
#define WUDEC(name, _callback) fmt_##name,
#include "dec.def"
#undef WUDEC
};

static const struct fmt_fn_desc fn_map[] = {
#define WUDEC(name, has_callback) { #name , has_callback },
#include "dec.def"
#undef WUDEC
};


/* Be careful with masks. This array is sorted dumbly. */
static struct fmt_magic magic_map[] = {
#ifdef WU_ENABLE_DIB
	{"\xff\xff", "BM", fmt_bmp},
#endif // WU_ENABLE_DIB

#ifdef WU_ENABLE_DPX
	{"\xff\xff\xff\xff" "\0\0\0\0" "\xff\x00\xff\xff",
		"XPDS\0\0\0\0V\0.0", fmt_dpx},
	{"\xff\xff\xff\xff" "\0\0\0\0" "\xff\x00\xff\xff",
		"SDPX\0\0\0\0V\0.0", fmt_dpx},
#endif // WU_ENABLE_DPX

#ifdef WU_ENABLE_FARBFELD
	{"\xff\xff\xff\xff" "\xff\xff\xff\xff", "farbfeld", fmt_farbfeld},
#endif // WU_ENABLE_FARBFELD

#ifdef WU_ENABLE_HG3
	{"\xff\xff\xff\xff", "HG-3", fmt_hg3},
#endif //WU_ENABLE_HG3

#ifdef WU_ENABLE_MAG
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "MAKI02  ", fmt_mag},
#endif //WU_ENABLE_MAG

#ifdef WU_ENABLE_MAKI
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "MAKI01A ", fmt_maki},
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "MAKI01B ", fmt_maki},
#endif //WU_ENABLE_MAKI

#ifdef WU_ENABLE_MSX
	// Maybe this is too general.
	//{"\xff\xff\xff\x00\x00\xff\xff", "\xfe\x00\x00\x00\x00\x00\x00", fmt_scr2},
#endif // WU_ENABLE_MSX

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
	{"\xff\xff", "PF", fmt_pnm}, // Color PFM
	{"\xff\xff", "Pf", fmt_pnm}, // Gray PFM

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
	{"\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftypavif", fmt_avif},
	{"\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftypavic", fmt_avif},
	{"\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff",
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
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftypheic", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftypheix", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftypheim", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftypheis", fmt_heif},

	// hevc|hevx|hevm|hevs
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftyphevc", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftyphevx", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftyphevm", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftyphevs", fmt_heif},

	// mif1|msf1
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftypmif1", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\0" "ftypmsf1", fmt_heif},
#endif // WU_ENABLE_HEIF

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
	{"\xff\xff\xff\xff\0\0\0\0\xff\xff\xff\xff", "RIFF\0\0\0\0WEBP", fmt_webp},
#endif // WU_ENABLE_WEBP
};


/* File extensions. An enum is used where the format has no clear magic
 * sequence, or where it must be treated specially. Otherwise, use -1 */
static struct fmt_ext ext_map[] = {
#ifdef WU_ENABLE_AVS
	{"avs", fmt_avs},
	{"mbfavs", fmt_avs},
#endif

#ifdef WU_ENABLE_DIB
	{"bmp", -1},
	{"bmp24", -1},
	{"cur", fmt_ico},
	{"dib", fmt_dib},
	{"ico", fmt_ico},
#endif

#ifdef WU_ENABLE_DPX
	{"dpx", -1},
#endif

#ifdef WU_ENABLE_FARBFELD
	{"ff", -1},
#endif

#ifdef WU_ENABLE_G00
	{"g00", fmt_g00},
#endif

#ifdef WU_ENABLE_HG3
	{"hg3", -1},
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
	{"sc2", fmt_scr2},
	{"grp", fmt_scr2},

	{"sc3", fmt_scr3},
	{"sc4", fmt_scr4},

	{"sc5", fmt_scr5},
	{"sr5", fmt_scr5}, // Graph Saurus raw
	{"ge5", fmt_scr5},

	{"sc6", fmt_scr6},
	{"s16", fmt_scr6}, // Alternate field of an SC7 file
	{"sr6", fmt_scr6}, // Graph Saurus raw

	{"sc7", fmt_scr7},
	{"s17", fmt_scr7}, // Alternate field of an SC7 file
	{"sr7", fmt_scr7},
	{"ge7", fmt_scr7},
	{"gl7", fmt_scr7},

	{"sc8", fmt_scr8},
	{"sr8", fmt_scr8},
	{"ge8", fmt_scr8},
	{"gl8", fmt_scr8},

	{"sca", fmt_scr10},

	{"scc", fmt_scr12},
	{"srs", fmt_scr12},
	{"yjk", fmt_scr12},
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

#ifdef WU_ENABLE_PIC
	{"jpc", -1},
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
	{"p7", -1},
	{"pgx", -1},
#endif

#ifdef WU_ENABLE_PRT
	{"prt", -1},
#endif

#ifdef WU_ENABLE_PX
	{"px", fmt_px},
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
	{"tga", fmt_tga},
#endif

#ifdef WU_ENABLE_TIM
	{"tim", fmt_tim},
#endif

#ifdef WU_ENABLE_TLG
	{"tlg", -1},
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
#endif

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
	// TIFF
	{"arw", fmt_raw},
	{"cr2", fmt_raw},
	{"dng", fmt_raw},
	{"nef", fmt_raw},
	{"pef", fmt_raw},

	{"orf", -1},
	{"raw", -1},
	{"rw2", -1},
#endif

#ifdef WU_ENABLE_SVG
	{"svg", fmt_svg},
	{"svgz", fmt_svg},
#endif

#ifdef WU_ENABLE_TIFF
	{"tif", -1},
	{"tiff", -1},
#ifndef WU_ENABLE_RAW
	/* Some RAW formats are just TIFF with extra data. Usually only a
	 * thumbnail will be shown, but it's better than nothing. */
	{"arw", -1},
	{"cr2", -1},
	{"dng", -1},
	{"nef", -1},
	{"pef", -1},
#endif // !WU_ENABLE_RAW
#endif // WU_ENABLE_TIFF

#ifdef WU_ENABLE_WEBP
	{"webp", -1},
#endif
};

/* Mime types. Useful for .desktop files. */
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

/* These are different from the ones in dec_fmtmap_base.c */
static int quine_fmaskmagiccmp(const void *restrict m1, const void *restrict m2) {
	const struct fmt_magic *restrict magic1 = m1;
	const struct fmt_magic *restrict magic2 = m2;
	int diff = 0;
	for (size_t i = 0; i < sizeof(magic1->bytes) && !diff; ++i) {
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

static void print_fn_def(const char *qual, const char *name,
const char *suffix, const bool is_callback) {
	const char *extra_args = (is_callback)
		? ",struct wu_state *state, enum image_event ev" : "";
	fprintf(stdout, "%s enum wu_error %.8s_%s("
		"struct image_file *infile, const struct wu_conf *wuconf %s);",
		qual, name, suffix, extra_args);
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

static int mapsort(void) {
	qsort(magic_map, ARRAY_LEN(magic_map), sizeof(*magic_map),
		quine_fmaskmagiccmp);
	qsort(ext_map, ARRAY_LEN(ext_map), sizeof(*ext_map), quine_fextcmp);

	print_include("\"dec_fn.h\"");

	/* Function pointer typedef */
	print_fn_def("typedef", "(*fmt", "dec_t)", false);
	print_fn_def("typedef", "(*fmt", "callback_t)", true);

	/* Struct maps definition string */
	fwrite(fmt_structs, 1, sizeof(fmt_structs) - 1, stdout);

	/* The maps proper */
	print_map_def("fn");
	for (size_t i = 0; i < ARRAY_LEN(fn_map); ++i) {
		fputs("{{", stdout);
		const int outlen = (int)print_hex(fn_map[i].name,
			sizeof(fn_map->name));
		fputs("},", stdout);

		fprintf(stdout, "%.*s_dec,", outlen, fn_map[i].name);
		if (fn_map[i].has_callback) {
			fprintf(stdout, "%.*s_callback", outlen, fn_map[i].name);
		} else {
			fputs("NULL", stdout);
		}
		fputs("},", stdout);
	}
	fputs("};", stdout);

	size_t max_mag_len = 0;
	size_t min_mag_len = SIZE_MAX;
	print_map_def("magic");
	for (size_t i = 0; i < ARRAY_LEN(magic_map); ++i) {
		if (i && !memcmp(magic_map + i, magic_map + i-1, sizeof(*magic_map))) {
			continue;
		}
		fputs("{{", stdout);
		const size_t bytes_len = sizeof(magic_map->bytes);
		const size_t outlen = print_hex(magic_map[i].and_mask,
			bytes_len);

		fputs("},{", stdout);
		print_hex(magic_map[i].bytes, bytes_len);
		fprintf(stdout, "},%d},", magic_map[i].id);

		if (outlen > max_mag_len) {
			max_mag_len = outlen;
		}
		if (outlen < min_mag_len) {
			min_mag_len = outlen;
		}
	}
	fputs("};", stdout);

	size_t max_ext_len = 0;
	size_t min_ext_len = SIZE_MAX;
	print_map_def("ext");
	for (size_t i = 0; i < ARRAY_LEN(ext_map); ++i) {
		if (i && !memcmp(ext_map + i, ext_map + i-1, sizeof(*ext_map))) {
			continue;
		}
		fputs("{{", stdout);
		const size_t outlen =  print_hex(ext_map[i].ext,
			sizeof(ext_map->ext));
		fprintf(stdout, "},%d},", ext_map[i].id);

		if (outlen > max_ext_len) {
			max_ext_len = outlen;
		}
		if (outlen < min_ext_len) {
			min_ext_len = outlen;
		}
	}
	fputs("};", stdout);

	fprintf(stdout,
		"static const size_t MAX_MAG_LEN = %zu;"
		"static const size_t MIN_MAG_LEN = %zu;"
		"static const size_t MAX_EXT_LEN = %zu;"
		"static const size_t MIN_EXT_LEN = %zu;",
		max_mag_len, min_mag_len,
		max_ext_len, min_ext_len);

	/* Include the rest of the file */
	fputs("\n#include \"dec.c\"\n", stdout);
	return 0;
}

static int dec_headers(void) {
	print_include("\"wudefs.h\"");
	for (size_t i = 0; i < ARRAY_LEN(fn_map); ++i) {
		print_fn_def("", fn_map[i].name, "dec", false);
		if (fn_map[i].has_callback) {
			print_fn_def("", fn_map[i].name, "callback", true);
		}
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
