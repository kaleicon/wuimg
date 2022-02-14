#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "dec_enable.def"

// Placeholders
typedef int dec_func_t;
typedef int dec_callback_t;

#define EXP_STRING(exp) #exp; exp
static const char search_structs[] = EXP_STRING(
	struct fmt_dec {
		const char name[8];
		const dec_func_t dec;
		const dec_callback_t callback;
	};

	struct fmt_ext {
		const char ext[8];
		const int id;
	};

	struct fmt_magic {
		const unsigned char and_mask[12];
		const unsigned char bytes[12];
		const int id;
	};
) /* EXP_STRING search_structs end */

enum format_id {
	fmt_unknown = -1,
#define WUDEC(name, callback) fmt_##name,
#include "dec.def"
#undef WUDEC
};

static const struct fmt_dec dec_map[] = {
#define WUDEC(name, callback) fmt_##name,
#include "dec.def"
#undef WUDEC
};

/* Be careful with masks. This array is sorted dumbly. Masks should cover
 * _continuous_ ranges of valid inputs. */
static struct fmt_magic magic_map[] = {
#ifdef WU_ENABLE_DIB
	{"\xff\xff", "BM", fmt_bmp},
#endif // WU_ENABLE_DIB

#ifdef WU_ENABLE_PCX
	// Second byte is version. Valid values are 0,2,3,4,5
	{"\xff\xff\xff", "\x0a\x00\x01", fmt_pcx}, // 0
	{"\xff\xfe\xff", "\x0a\x02\x01", fmt_pcx}, // 2,3
	{"\xff\xfe\xff", "\x0a\x04\x01", fmt_pcx}, // 4,5

	{"\xff\xff\xff\xff", "\xb1\x68\xde\x3a", fmt_dcx},
#endif // WU_ENABLE_PCX

#ifdef WU_ENABLE_PGX
	{"\xff\xff\xff\xff", "PGX\0", fmt_pgx},
#endif // WU_ENABLE_PGX

#ifdef WU_ENABLE_PI
	{"\xff\xff", "Pi", fmt_pi},
#endif // WU_ENABLE_PI

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
#endif // WU_ENABLE_PNM

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

#ifdef WU_ENABLE_WBM
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "WPX\x1a" "BMP", fmt_wbm},
#endif // WU_ENABLE_WBM

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


#ifdef WU_ENABLE_FLIF
	{"\xff\xff\xff\xff", "FLIF", fmt_flif},
#endif // WU_ENABLE_FLIF

#ifdef WU_ENABLE_GIF
	{"\xff\xff\xff\xff\xff\xff", "GIF87a", fmt_gif},
	{"\xff\xff\xff\xff\xff\xff", "GIF89a", fmt_gif},
#endif // WU_ENABLE_GIF

#ifdef WU_ENABLE_HEIF
	/* HEIF follows ISOBMFF, so we can't stop at 'ftyp' or try to get
	 * clever with masks, or we could match a few hundred other formats.
	 * https://github.com/file/file/blob/master/magic/Magdir/animation

	 * The first 32-bit word (little-endian) is an offset to something not
	 * relevant to us. I've only seen values is the range 0x18-0x30, so it
	 * ought to be safe to depend only on the fourth byte. The offset must
	 * also be a multiple of 4, so the two lower bits should be zero and
	 * not be masked.

	 * Finally, JPEG2000 can also start with 3 zero bytes, followed
	 * by 0x0c. Since the compile-time sorter ignores the mask, we ensure
	 * HEIF sorts after JP2 by making the fourth byte greater than 0x0c. */

	/* AVIF */
	{"\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xff" "ftypavif", fmt_avif},
	{"\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xff" "ftypavis", fmt_avif},

	/* HEIF */
	// heic|heix|heim|heis
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftypheic", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftypheix", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftypheim", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftypheis", fmt_heif},

	// hevc|hevx|hevm|hevs
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftyphevc", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftyphevx", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftyphevm", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftyphevs", fmt_heif},

	// avic|avis
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftypavic", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftypavis", fmt_heif},

	// mif1|msf1
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftypmif1", fmt_heif},
	{"\xff\xff\xff\x03\xff\xff\xff\xff\xff\xff\xff\xff",
		"\0\0\0\xfc" "ftypmsf1", fmt_heif},
#endif // WU_ENABLE_HEIF

#ifdef WU_ENABLE_JPEG
	{"\xff\xff\xff", "\xff\xd8\xff", fmt_jpeg},
#endif // WU_ENABLE_JPEG

#ifdef WU_ENABLE_JPEG2000
	{"\xff\xff\xff\xff" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\x0c" "jP\x20\x20" "\r\n\x87\n", fmt_jp2},
	{"\xff\xff\xff\xff", "\r\n\x87\n", fmt_jp2},
	{"\xff\xff\xff\xff", "\xff\x4f\xff\x51", fmt_j2k},
#endif // WU_ENABLE_JPEG2000

#ifdef WU_ENABLE_PNG
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "\x89PNG\r\n\x1a\n", fmt_png},
#endif // WU_ENABLE_PNG

#ifdef WU_ENABLE_RAW
	// Olympus ORF
	{"\xff\xff\xff\xff", "IIRS", fmt_raw},
	{"\xff\xff\xff\xff", "IIRO", fmt_raw},
	{"\xff\xff\xff\xff", "MMOR", fmt_raw},

	// Panasonic RAW/RW2
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "IIU\x00\x08\x00\0\0", fmt_raw},
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
 * sequence, or where it must be treated specially. */
static struct fmt_ext extension_map[] = {
#ifdef WU_ENABLE_AVS
	{"avs", fmt_avs},
	{"mbfavs", fmt_avs},
#endif // WU_ENABLE_AVS

#ifdef WU_ENABLE_DIB
	{"bmp", -1},
	{"bmp24", -1},
	{"dib", fmt_dib},
	{"ico", fmt_ico},
#endif // WU_ENABLE_DIB

#ifdef WU_ENABLE_G00
	{"g00", fmt_g00},
#endif // WU_ENABLE_DIB

#ifdef WU_ENABLE_MAC
	{"mac", fmt_mac},
	{"pntg", fmt_mac},
#endif // WU_ENABLE_MAC

#ifdef WU_ENABLE_PCX
	{"dcx", -1},
	{"pcc", -1},
	{"pcx", -1},
#endif // WU_ENABLE_PCX

#ifdef WU_ENABLE_PGX
	{"pgx", -1},
#endif // WU_ENABLE_PGX

#ifdef WU_ENABLE_PI
	{"pi", -1},
#endif // WU_ENABLE_PI

#ifdef WU_ENABLE_PICTOR
	{"pic", -1},
#endif // WU_ENABLE_PI

#ifdef WU_ENABLE_PNM
	{"mtv", fmt_pnm},
	{"pbm", -1},
	{"pgm", -1},
	{"ppm", -1},
	{"pam", -1},
	{"pnm", -1},
	{"pfm", -1},
	{"p7", -1},
#endif // WU_ENABLE_PNM

#ifdef WU_ENABLE_SGI
	{"bw", -1},
	{"rgb", -1},
	{"rgba", -1},
	{"sgi", -1},
#endif // WU_ENABLE_SGI

#ifdef WU_ENABLE_SIXEL
	{"six", fmt_sixel},
	{"sixel", fmt_sixel},
#endif // WU_ENABLE_SIXEL

#ifdef WU_ENABLE_SUN
	{"im1", -1},
	{"im4", -1},
	{"im8", -1},
	{"im24", -1},
	{"im32", -1},
	{"ras", -1},
	{"sun", -1},
#endif // WU_ENABLE_SUN

#ifdef WU_ENABLE_TGA
	{"tga", fmt_tga},
#endif // WU_ENABLE_TGA

#ifdef WU_ENABLE_TIM
	{"tim", fmt_tim},
#endif // WU_ENABLE_TIM

#ifdef WU_ENABLE_TLG
	{"tlg", -1},
#endif // WU_ENABLE_TLG

#ifdef WU_ENABLE_WBM
	{"wbm", -1},
#endif // WU_ENABLE_WBM

#ifdef WU_ENABLE_WBMP
	{"wbmp", fmt_wbmp},
#endif // WU_ENABLE_WBMP

#ifdef WU_ENABLE_XBM
	{"xbm", fmt_xbm},
#endif // WU_ENABLE_XBM


#ifdef WU_ENABLE_FLIF
	{"flif", -1},
#endif // WU_ENABLE_FLIF

#ifdef WU_ENABLE_GIF
	{"gif", -1},
	{"gif87", -1},
	{"gif89", -1},
#endif // WU_ENABLE_GIF

#ifdef WU_ENABLE_HEIF
	{"avif", -1},
	{"avifs", -1},
	{"heic", -1},
	{"heics", -1},
	{"heif", -1},
	{"heifs", -1},
	{"hif", -1},
#endif // WU_ENABLE_HEIF

#ifdef WU_ENABLE_JBIG
	{"bie", fmt_jbig},
	{"jbg", fmt_jbig},
	{"jbig", fmt_jbig},
#endif // WU_ENABLE_JBIG

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
#endif // WU_ENABLE_JPEG

#ifdef WU_ENABLE_JPEG2000
	{"j2k", -1},
	{"jp2", -1},
	{"jpc", -1},
#endif // WU_ENABLE_JPEG2000

#ifdef WU_ENABLE_PNG
	{"png", -1},
#endif // WU_ENABLE_PNG

#ifdef WU_ENABLE_RAW
	{"cr2", fmt_raw},
	{"dng", fmt_raw},
	{"nef", fmt_raw},
	{"orf", -1},
	{"raw", -1},
	{"rw2", -1},
#endif

#ifdef WU_ENABLE_SVG
	{"svg", fmt_svg},
	{"svgz", fmt_svg},
#endif // WU_ENABLE_SVG

#ifdef WU_ENABLE_TIFF
	{"tif", -1},
	{"tiff", -1},
#ifndef WU_ENABLE_RAW
	/* Some RAW formats are just TIFF with extra data. Usually only a
	 * thumbnail will be shown, but it's better than nothing. */
	{"cr2", -1},
	{"dng", -1},
	{"nef", -1},
#endif // !WU_ENABLE_RAW
#endif // WU_ENABLE_TIFF

#ifdef WU_ENABLE_WEBP
	{"webp", -1},
#endif // WU_ENABLE_WEBP

};

/* These are different from the ones in dec_fmtmap_base.c */
static int quine_fmaskmagiccmp(const void *restrict m1, const void *restrict m2) {
	const struct fmt_magic *restrict magic1 = m1;
	const struct fmt_magic *restrict magic2 = m2;
	const unsigned char *and_mask1 = magic1->and_mask;
	const unsigned char *and_mask2 = magic2->and_mask;
	int diff = 0;
	for (size_t i = 0; i < sizeof(magic2->bytes) && !diff; ++i) {
		const unsigned char c1 = and_mask1[i];
		const unsigned char c2 = and_mask2[i];
		diff = (magic1->bytes[i] & c1) - (magic2->bytes[i] & c2);
	}
	return diff;
}

static int quine_fextcmp(const void *restrict e1, const void *restrict e2) {
	const struct fmt_ext *restrict ext1 = e1;
	const struct fmt_ext *restrict ext2 = e2;
	return memcmp(ext1->ext, ext2->ext, sizeof(ext2->ext));
}

static size_t print_hex_string(const void *str, size_t len, FILE *outfile) {
	const unsigned char *bytes = str;
	while (len && !bytes[len - 1]) {
		--len;
	}
	for (size_t k = 0; k < len; ++k) {
		fprintf(outfile, "%#hhx,", bytes[k]);
	}
	return len;
}

static int fmtsort(void) {
	qsort(magic_map, ARRAY_LEN(magic_map), sizeof(*magic_map),
		quine_fmaskmagiccmp);
	qsort(extension_map, ARRAY_LEN(extension_map), sizeof(*extension_map),
		quine_fextcmp);


	/* size_t definition */
	fputs("#include <stddef.h>\n", stdout);

	/* Struct maps definition */
	fwrite(search_structs, 1, sizeof(search_structs) - 1, stdout);

	/* The maps proper, with added const */
	size_t max_mag_len = 0;
	size_t min_mag_len = SIZE_MAX;
	fputs("static const struct fmt_magic magic_map[] = {", stdout);
	for (size_t i = 0; i < ARRAY_LEN(magic_map); ++i) {
		fputs("{{", stdout);

		const size_t bytes_len = sizeof(magic_map->bytes);
		const size_t outlen = print_hex_string(magic_map[i].and_mask,
			bytes_len, stdout);

		fputs("}, {", stdout);

		print_hex_string(magic_map[i].bytes, bytes_len, stdout);
		fprintf(stdout, "}, %d},", magic_map[i].id);

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
	fputs("static const struct fmt_ext extension_map[] = {", stdout);
	for (size_t i = 0; i < ARRAY_LEN(extension_map); ++i) {
		fputs("{{", stdout);

		const size_t ext_len = sizeof(extension_map->ext);
		size_t outlen = print_hex_string(extension_map[i].ext,
			ext_len, stdout);

		fprintf(stdout, "}, %d},", extension_map[i].id);

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
	fputs("\n#include \"dec_fmtmap.c\"\n", stdout);
	return 0;
}

static int echo_includes(const int argc, const char *argv[]) {
	for (int i = 0; i < argc; ++i) {
		fputs("#include \"", stdout);
		fputs(argv[i], stdout);
		fputs("\"\n", stdout);
	}
	return 0;
}

int main(const int argc, const char *argv[]) {
	if (argc > 1) {
		if (!strcmp(argv[1], "-f")) {
			return fmtsort();
		} else if (!strcmp(argv[1], "-h")) {
			return echo_includes(argc - 2, argv + 2);
		}
	}
	return 1;
}
