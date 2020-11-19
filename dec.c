#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "wudefs.h"
#include "common.h"
#include "dec.h"

#include "dec_includes.h"
/*
#include "dec/avs.h"
#include "dec/bmp.h"
#include "dec/mac.h"
#include "dec/pcx.h"
#include "dec/pgx.h"
#include "dec/pi.h"
#include "dec/pnm.h"
#include "dec/sgi.h"
#include "dec/sixel.h"
#include "dec/sun.h"
#include "dec/tga.h"
#include "dec/wbmp.h"
#include "dec/xbm.h"

#include "dec/flif.h"
#include "dec/gif.h"
#include "dec/heif.h"
#include "dec/jbig.h"
#include "dec/jpeg.h"
#include "dec/jpeg2000.h"
#include "dec/png.h"
#include "dec/raw.h"
#include "dec/svg.h"
#include "dec/tiff.h"
#include "dec/webp.h"
*/
typedef enum wu_error (*dec_func_t)(struct image_file *infile,
	const struct wu_conf *wuconf);

typedef enum wu_error (*dec_callback_t)(struct image_file *infile,
	const struct wu_conf *wuconf, struct wu_state *state,
	enum image_event);


enum format_id {
	fmt_unknown = -1,
#define WUDEC(name, callback) fmt_##name,
#include "dec.def"
#undef WUDEC
	nb_of_fmts,
};

struct format_fn {
	const char name[8];
	const dec_func_t dec;
	const dec_callback_t callback;
};

struct file_ext {
	char ext[8];
	const enum format_id id;
};

struct file_bytes {
	const unsigned char mask[12];
	unsigned char bytes[12];
	const enum format_id id;
};

static const struct format_fn format_map[nb_of_fmts] = {
#define WUDEC(name, callback) {#name, name##_dec, callback},
#include "dec.def"
#undef WUDEC
};

static const size_t MIN_MAGIC_LEN = 2; // Anything shorter is meaningless
static struct file_bytes magic_map[] = {
#ifdef DEC_BMP
	{"\xff\xff", "BM", fmt_bmp},
#endif // DEC_BMP

#ifdef DEC_PCX
	{"\xff\xff\xff", "\x0a\x00\x01", fmt_pcx},
	{"\xff\xff\xff", "\x0a\x02\x01", fmt_pcx},
	{"\xff\xff\xff", "\x0a\x03\x01", fmt_pcx},
	{"\xff\xff\xff", "\x0a\x04\x01", fmt_pcx},
	{"\xff\xff\xff", "\x0a\x05\x01", fmt_pcx},

	{"\xff\xff\xff\xff", "\xb1\x68\xde\x3a", fmt_dcx},
#endif // DEC_PCX

#ifdef DEC_PGX
	{"\xff\xff\xff\xff", "PGX\0", fmt_pgx},
#endif // DEC_PGX

#ifdef DEC_PI
	{"\xff\xff", "Pi", fmt_pi},
#endif // DEC_PI

#ifdef DEC_PNM
	{"\xff\xff", "P1", fmt_pnm},
	{"\xff\xff", "P2", fmt_pnm},
	{"\xff\xff", "P3", fmt_pnm},
	{"\xff\xff", "P4", fmt_pnm},
	{"\xff\xff", "P5", fmt_pnm},
	{"\xff\xff", "P6", fmt_pnm},
	{"\xff\xff\xff", "P7\n", fmt_pnm}, // PAM
	{"\xff\xff\xff\xff\xff\xff\xff", "P7 332\n", fmt_pnm}, // Xv thumbnail
	{"\xff\xff", "PF", fmt_pnm}, // Color PFM
	{"\xff\xff", "Pf", fmt_pnm}, // Gray PFM
#endif // DEC_PNM

#ifdef DEC_SGI
	{"\xff\xff", "\x01\xda", fmt_sgi},
#endif // DEC_SGI

#ifdef DEC_SIXEL
	// {"\xff", "\x90", fmt_sixel}, // Valid but too short
	{"\xff\xff", "\x1bP", fmt_sixel},
#endif // DEC_SIXEL

#ifdef DEC_SUN
	{"\xff\xff\xff\xff", "\x59\xa6\x6a\x95", fmt_sun},
#endif // DEC_SUN

#ifdef DEC_XBM
	{"\xff\xff", "\x2f\2a", fmt_xbm}, /* C comment, only in hex because my
		editor tells me it will comment away half the map otherwise. */
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "#define ", fmt_xbm},
#endif // DEC_XBM


#ifdef DEC_FLIF
	{"\xff\xff\xff\xff", "FLIF", fmt_flif},
#endif // DEC_FLIF

#ifdef DEC_GIF
	{"\xff\xff\xff\xff\xff\xff", "GIF87a", fmt_gif},
	{"\xff\xff\xff\xff\xff\xff", "GIF89a", fmt_gif},
#endif // DEC_GIF

#ifdef DEC_HEIF
	// Fourth byte must be less than \x0c to avoid mess ups with JP2.
	{"\xff\xff\xff\x00\xff\xff\xff\xff", "\0\0\0\x00" "ftyp", fmt_heif},
#endif // DEC_HEIF

#ifdef DEC_JPEG
	{"\xff\xff\xff", "\xff\xd8\xff", fmt_jpeg},
#endif // DEC_JPEG

#ifdef DEC_JPEG2000
	{"\xff\xff\xff\xff" "\xff\xff\xff\xff" "\xff\xff\xff\xff",
		"\0\0\0\x0c" "jP\x20\x20" "\r\n\x87\n", fmt_jp2},
	{"\xff\xff\xff\xff", "\r\n\x87\n", fmt_jp2},
	{"\xff\xff\xff\xff", "\xff\x4f\xff\x51", fmt_j2k},
#endif // DEC_JPEG2000

#ifdef DEC_PNG
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "\x89PNG\r\n\x1a\n", fmt_png},
#endif // DEC_PNG

#ifdef DEC_RAW
	// Olympus ORF
	{"\xff\xff\xff\xff", "IIRS", fmt_raw},
	{"\xff\xff\xff\xff", "IIRO", fmt_raw},
	{"\xff\xff\xff\xff", "MMOR", fmt_raw},

	// Panasonic RAW/RW2
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "IIU\x00\x08\x00\0\0", fmt_raw},
#endif // DEC_RAW

#ifdef DEC_TIFF
	{"\xff\xff\xff\xff", "II\x2a\x00", fmt_tiff},
	{"\xff\xff\xff\xff", "MM\x00\x2a", fmt_tiff},

	// BigTIFF
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "II\x2b\x00\x08\x00\0\0", fmt_tiff},
	{"\xff\xff\xff\xff\xff\xff\xff\xff", "MM\x00\x2b\x00\x08\0\0", fmt_tiff},
#endif // DEC_TIFF

#ifdef DEC_WEBP
	{"\xff\xff\xff\xff\0\0\0\0\xff\xff\xff\xff", "RIFF\0\0\0\0WEBP", fmt_webp},
#endif // DEC_WEBP
};

/* File extensions for filtering and fallbacks.
 * An enum is used for formats that lack a magic bytes sequence. */
static struct file_ext extension_map[] = {
#ifdef DEC_AVS
	{"avs", fmt_avs},
	{"mbfavs", fmt_avs},
#endif // DEC_AVS

#ifdef DEC_BMP
	{"bmp", -1},
	{"bmp24", -1},
#endif // DEC_BMP

#ifdef DEC_MAC
	{"mac", fmt_mac},
	{"pntg", fmt_mac},
#endif // DEC_MAC

#ifdef DEC_PCX
	{"dcx", -1},
	{"pcc", -1},
	{"pcx", -1},
#endif // DEC_PCX

#ifdef DEC_PGX
	{"pgx", -1},
#endif // DEC_PGX

#ifdef DEC_PI
	{"g", -1},
	{"pi", -1},
#endif // DEC_PI

#ifdef DEC_PNM
	{"mtv", fmt_pnm},
	{"pbm", -1},
	{"pgm", -1},
	{"ppm", -1},
	{"pam", -1},
	{"pnm", -1},
	{"p7", -1},
#endif // DEC_PNM

#ifdef DEC_SGI
	{"bw", -1},
	{"rgb", -1},
	{"rgba", -1},
	{"sgi", -1},
#endif // DEC_SGI

#ifdef DEC_SIXEL
	{"six", -1},
	{"sixel", -1},
#endif // DEC_SIXEL

#ifdef DEC_SUN
	{"im1", -1},
	{"im4", -1},
	{"im8", -1},
	{"im24", -1},
	{"im32", -1},
	{"ras", -1},
	{"sun", -1},
#endif // DEC_SUN

#ifdef DEC_TGA
	{"tga", fmt_tga},
#endif // DEC_TGA

#ifdef DEC_WBMP
	{"wbmp", fmt_wbmp},
#endif // DEC_WBMP

#ifdef DEC_XBM
	{"xbm", fmt_xbm},
#endif // DEC_XBM


#ifdef DEC_FLIF
	{"flif", -1},
#endif // DEC_FLIF

#ifdef DEC_GIF
	{"gif", -1},
	{"gif87", -1},
	{"gif89", -1},
#endif // DEC_GIF

#ifdef DEC_HEIF
	{"avif", -1},
	{"avifs", -1},
	{"heic", -1},
	{"heics", -1},
	{"heif", -1},
	{"heifs", -1},
	{"hif", -1},
#endif // DEC_HEIF

#ifdef DEC_JBIG
	{"bie", fmt_jbig},
	{"jbg", fmt_jbig},
	{"jbig", fmt_jbig},
#endif // DEC_JBIG

#ifdef DEC_JPEG
	{"jfi", -1},
	{"jfif", -1},
	{"jif", -1},
	{"jpe", -1},
	{"jpeg", -1},
	{"jpg", -1},
	{"jps", -1},
	{"mpo", -1},
	{"thm", -1},
#endif // DEC_JPEG

#ifdef DEC_JPEG2000
	{"j2k", -1},
	{"jp2", -1},
	{"jpc", -1},
#endif // DEC_JPEG2000

#ifdef DEC_PNG
	{"png", -1},
#endif // DEC_PNG

#ifdef DEC_RAW
	{"cr2", fmt_raw},
	{"dng", fmt_raw},
	{"nef", fmt_raw},
	{"orf", -1},
	{"raw", -1},
	{"rw2", -1},
#endif

#ifdef DEC_SVG
	{"svg", fmt_svg},
	{"svgz", fmt_svg},
#endif // DEC_SVG

#ifdef DEC_TIFF
	{"tif", -1},
	{"tiff", -1},
#ifndef DEC_RAW
	// Results aren't very good but it's better than nothing
	{"cr2", -1},
	{"dng", -1},
	{"nef", -1},
#endif // DEC_RAW
#endif // DEC_TIFF

#ifdef DEC_WEBP
	{"webp", -1},
#endif // DEC_WEBP

};


static int fextcmp(const void *restrict e1, const void *restrict e2) {
	const struct file_ext *restrict ext1 = e1;
	const struct file_ext *restrict ext2 = e2;
	return memcmp(ext1->ext, ext2->ext, sizeof(ext1->ext));
}

static int fmaskbytescmp(const void *restrict m1, const void *restrict m2) {
	const struct file_bytes *restrict magic1 = m1;
	const struct file_bytes *restrict magic2 = m2;
	const unsigned char *mask = magic2->mask;
	int diff = 0;
	for (size_t i = 0; i < sizeof(magic1->bytes) && !diff; ++i) {
		const int m = mask[i];
		diff = (magic1->bytes[i] & m) - (magic2->bytes[i] & m);
	}
	return diff;
}

static const struct file_ext * search_extension(const char *filename,
const size_t len) {
	struct file_ext fext = {0};
	const size_t start = len - zumin(len, sizeof(fext.ext) + 1 /* dot */);
	const char *ext = strrchr(filename + start, '.');
	if (ext) {
		++ext;

		const size_t ext_len = (size_t)(filename + len - ext);
		if (ext_len) {
			for (size_t i = 0; i < ext_len; ++i) {
				fext.ext[i] = (char)tolower(ext[i]);
			}
			return bsearch(&fext, extension_map,
				ARRAY_LEN(extension_map),
				sizeof(*extension_map), fextcmp);
		}
	}
	return NULL;
}

static const struct file_bytes * search_magic(FILE *ifp) {
	struct file_bytes in = {0};
	if (fread(in.bytes, 1, sizeof(in.bytes), ifp) > MIN_MAGIC_LEN) {
		return bsearch(&in, magic_map, ARRAY_LEN(magic_map),
			sizeof(*magic_map), fmaskbytescmp);
	}
	return NULL;
}

bool known_extension(const char *filename) {
	return (bool)search_extension(filename, strlen(filename));
}

static enum format_id try_extension(const char *filename) {
	const struct file_ext *dec = search_extension(filename, strlen(filename));
	if (dec) {
		return dec->id;
	}
	return fmt_unknown;
}

static enum format_id try_magic(FILE *ifp) {
	const struct file_bytes *dec = search_magic(ifp);
	if (dec) {
		return dec->id;
	}
	return fmt_unknown;
}

static enum format_id find_decoder(FILE *ifp, const char *filename) {
	/* In most cases the magic bytes conclusively identify the file, but we
	 * try the extension first since a few formats must be treated
	 * specially. Most libraw formats are essentially TIFF, for instance.

	 * The real solution would be to handle those inside tiff_dec, but... */
	const enum format_id id = try_extension(filename);
	if (id == fmt_unknown) {
		return try_magic(ifp);
	}
	return id;
}

static enum format_id identify_image(FILE *ifp, const char *filename) {
	errno = 0;
	const enum format_id id = find_decoder(ifp, filename);
	if (id != fmt_unknown) {
		rewind(ifp);
	}
	return id;
}

enum wu_error callback_image(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	const enum format_id id = infile->fmt_id;
	return format_map[id].callback(infile, wuconf, state, event);
}

static void stat_metadata(struct wu_tree *tree, const int fd) {
	struct stat sb;
	if (fstat(fd, &sb) != 0) {
		return;
	}

	struct wu_tree *meta = tree_sprout_branch(tree, "Stats");
	if (!meta) {
		return;
	}

	const struct wu_tree_sap sap[] = {
		{"Size", wu_leaf_signed, {.d = sb.st_size}},
		{"Last access", wu_leaf_time, {.time = sb.st_atim.tv_sec}},
		{"Last modified", wu_leaf_time, {.time = sb.st_mtim.tv_sec}},
		{"Last status change", wu_leaf_time,
			{.time = sb.st_ctim.tv_sec}},
	};
	tree_bud_leaves(meta, sap, ARRAY_LEN(sap));
}

enum wu_error decode_image(struct image_file *infile,
const struct wu_conf *wuconf, const char *filename) {
	if (!infile->ifp) {
		errno = 0;
		infile->ifp = fopen(filename, "rb");
		if (!infile->ifp) {
			return wu_open_error;
		}
	}

	errno = 0;
	const enum format_id id = identify_image(infile->ifp, filename);
	if (id == fmt_unknown) {
		if (errno) {
			infile->err_msg = strdup(strerror(errno));
			return wu_open_error;
		}
		return wu_unknown_file_type;
	}

	if (!tree_sow(&infile->metadata, "Metadata")) {
		return wu_alloc_error;
	}
	tree_sprout_leaf(&infile->metadata, "Format", format_map[id].name);
	stat_metadata(&infile->metadata, fileno(infile->ifp));

	const enum wu_error result = format_map[id].dec(infile, wuconf);
	infile->fmt_id = id;
	if (result == wu_ok) {
		normalize_sub_images(infile);
	}
	return result;
}

static char * pathcat(const char *p1, const size_t p1_len, const char *p2,
const size_t p2_len) {
	// Assumes p1 has a trailing slash
	char *cat = malloc(p1_len + p2_len + 1);
	if (cat) {
		memcpy(cat, p1, p1_len);
		memcpy(cat + p1_len, p2, p2_len + 1);
	}
	return cat;
}

char ** filter_directory(const char *restrict dirname,
const char *restrict init_name, size_t *nr) {
	const size_t dir_len = strlen(dirname);
	DIR *dir = opendir(dir_len ? dirname : ".");
	if (!dir) {
		return NULL;
	}

	*nr = 64;
	char **names = malloc(*nr * sizeof(*names));
	if (!names) {
		closedir(dir);
		return NULL;
	}

	size_t init_name_len;
	size_t idx = 0;
	if (init_name) {
		init_name_len = strlen(init_name);
		names[idx] = pathcat(dirname, dir_len, init_name,
			init_name_len);
		++idx;
	} else {
		init_name_len = 0;
	}

	const struct dirent *entry;
	while ((entry = readdir(dir))) {
#ifdef _DIRENT_HAVE_D_TYPE
		switch (entry->d_type) {
		case DT_REG:
		case DT_LNK:
		case DT_UNKNOWN:
			break;
		default:
			continue;
		}
#endif
#ifdef _DIRENT_HAVE_D_NAMLEN
		const size_t name_len = (size_t)entry->d_namlen;
#else
		const size_t name_len = strlen(entry->d_name);
#endif
		// Avoid including 'init_name' twice.
		if (name_len == init_name_len) {
			if (!memcmp(init_name, entry->d_name, name_len)) {
				init_name_len = 0;
				continue;
			}
		}

		if (search_extension(entry->d_name, name_len)) {
			if (!grow_buffer(&names, nr, idx, sizeof(*names)) ) {
				break;
			}

			names[idx] = pathcat(dirname, dir_len,
				entry->d_name, name_len);
			if (names[idx]) {
				++idx;
			}
		}
	}
	closedir(dir);
	*nr = idx;
	if (idx == 0) {
		free(names);
		return NULL;
	}
	return names;
}

void sort_dec_tables(void) {
	qsort(magic_map, ARRAY_LEN(magic_map), sizeof(magic_map[0]),
		fmaskbytescmp);
	qsort(extension_map, ARRAY_LEN(extension_map),
		sizeof(extension_map[0]), fextcmp);
}

void print_known_formats(void) {
	sort_dec_tables();

	printf("Known formats: %zu\n", ARRAY_LEN(format_map));
	for (size_t i = 0; i < ARRAY_LEN(format_map); ++i) {
		fputs(format_map[i].name, stdout);
		if (i + 1 < ARRAY_LEN(format_map)) {
			fputs(", ", stdout);
		} else {
			fputs("\n\n", stdout);
		}
	}

	printf("Known extensions: %zu\n", ARRAY_LEN(extension_map));
	for (size_t i = 0; i < ARRAY_LEN(extension_map); ++i) {
		fputs(extension_map[i].ext, stdout);
		if (i + 1 < ARRAY_LEN(extension_map)) {
			fputs(", ", stdout);
		} else {
			fputs("\n\n", stdout);
		}
	}

	printf("Known magic sequences: %zu\n", ARRAY_LEN(magic_map));
}
