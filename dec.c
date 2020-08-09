#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>

#include <archive.h>
#include <archive_entry.h>

#include "wudefs.h"
#include "common.h"
#include "dec.h"

#include "dec_avs.h"
#include "dec_bmp.h"
#include "dec_pcx.h"
#include "dec_pi.h"
#include "dec_pnm.h"
#include "dec_sgi.h"
#include "dec_sun.h"
#include "dec_tga.h"
#include "dec_wbmp.h"
#include "dec_xbm.h"

#include "dec_flif.h"
#include "dec_gif.h"
#include "dec_heif.h"
#include "dec_jbig.h"
#include "dec_jpeg.h"
#include "dec_jpeg2000.h"
#include "dec_png.h"
#include "dec_raw.h"
#include "dec_svg.h"
#include "dec_tiff.h"
#include "dec_webp.h"

typedef enum wu_error (*dec_func_t)(struct image_file *infile,
	const struct wu_conf *wuconf);

typedef enum wu_error (*dec_callback_t)(struct image_file *infile,
	const struct wu_conf *wuconf, struct wu_state *state,
	enum image_event);

typedef bool (*magic_verify_t)(FILE *ifp);


enum format_id {
	fmt_unknown = -1,
#define WUDEC(name, callback, verify) fmt_##name,
#include "dec.def"
#undef WUDEC
	nb_of_fmts,
};

struct format_fn {
	const char name[8];
	const dec_func_t dec;
	const dec_callback_t callback;
	const magic_verify_t verify;
};

struct file_ext {
	char ext[8];
	const enum format_id id;
};

struct file_bytes {
	unsigned char bytes[16];
	unsigned int len;
	const enum format_id id;
};


static const struct format_fn format_map[nb_of_fmts] = {
#define WUDEC(name, callback, verify) {#name, name##_dec, callback, verify},
#include "dec.def"
#undef WUDEC
};

/* The obvious issue here is that a short sequence that matches the start of a
 * longer one will ruin searches. */
#define WITH_LEN(bytes) (bytes), (unsigned int)(sizeof(bytes) - 1)
static struct file_bytes magic_map[] = {
#ifdef DEC_BMP
	{WITH_LEN("BM"), fmt_bmp},
#endif // DEC_BMP

#ifdef DEC_PCX
	// All known versions
	{WITH_LEN("\x0a\x00\x01"), fmt_pcx},
	{WITH_LEN("\x0a\x02\x01"), fmt_pcx},
	{WITH_LEN("\x0a\x03\x01"), fmt_pcx},
	{WITH_LEN("\x0a\x04\x01"), fmt_pcx},
	{WITH_LEN("\x0a\x05\x01"), fmt_pcx},

	{WITH_LEN("\xb1\x68\xde\x3a"), fmt_dcx},
#endif // DEC_PCX

#ifdef DEC_PI
	{WITH_LEN("Pi"), fmt_pi},
#endif // DEC_PI

#ifdef DEC_PNM
	{WITH_LEN("P1"), fmt_pnm},
	{WITH_LEN("P2"), fmt_pnm},
	{WITH_LEN("P3"), fmt_pnm},
	{WITH_LEN("P4"), fmt_pnm},
	{WITH_LEN("P5"), fmt_pnm},
	{WITH_LEN("P6"), fmt_pnm},
	{WITH_LEN("P7\n"), fmt_pnm}, // PAM
	{WITH_LEN("P7 332\n"), fmt_pnm}, // Xv thumbnail
	{WITH_LEN("PF"), fmt_pnm}, // Color PFM
	{WITH_LEN("Pf"), fmt_pnm}, // Gray PFM
#endif // DEC_PNM

#ifdef DEC_SGI
	{WITH_LEN("\x01\xda"), fmt_sgi},
#endif // DEC_SGI

#ifdef DEC_SUN
	{WITH_LEN("\x59\xa6\x6a\x95"), fmt_sun},
#endif // DEC_SUN

#ifdef DEC_XBM
	{WITH_LEN("#define"), fmt_xbm},
#endif // DEC_XBM

#ifdef DEC_PNG
	{WITH_LEN("\x89PNG\r\n\x1a\n"), fmt_png},
#endif // DEC_PNG

#ifdef DEC_JPEG
	{WITH_LEN("\xff\xd8\xff"), fmt_jpeg},
#endif // DEC_JPEG

#ifdef DEC_GIF
	{WITH_LEN("GIF87a"), fmt_gif},
	{WITH_LEN("GIF89a"), fmt_gif},
#endif // DEC_GIF

#ifdef DEC_TIFF
	{WITH_LEN("II\x2a\x00"), fmt_tiff},
	{WITH_LEN("MM\x00\x2a"), fmt_tiff},

	// BigTIFF
	{WITH_LEN("II\x2b\x00\x08\x00\x00\x00"), fmt_tiff},
	{WITH_LEN("MM\x00\x2b\x00\x08\x00\x00"), fmt_tiff},
#endif // DEC_TIFF

#ifdef DEC_RAW
	// Olympus ORF
	{WITH_LEN("IIRS"), fmt_raw},
	{WITH_LEN("IIRO"), fmt_raw},
	{WITH_LEN("MMOR"), fmt_raw},

	// Panasonic RAW/RW2
	{WITH_LEN("IIU\x00\x08\x00\x00\x00"), fmt_raw},
#endif // DEC_RAW

#ifdef DEC_JPEG2000
	{WITH_LEN("\x00\x00\x00\x0c\x6a\x50\x20\x20\x0d\x0a\x87\x0a"), fmt_jp2},
	{WITH_LEN("\x0d\x0a\x87\x0a"), fmt_jp2},
	{WITH_LEN("\xff\x4f\xff\x51"), fmt_j2k},
#endif // DEC_JPEG2000

#ifdef DEC_WEBP
	// Various formats may start with "RIFF", notably WAV.
	{WITH_LEN("RIFF"), fmt_webp},
#endif // DEC_WEBP

#ifdef DEC_HEIF
	// Split string to prevent 'hex escape sequence out of range' error.
	{WITH_LEN("\x00\x00\x00\x18" "ftypheic"), fmt_heif},
#endif // DEC_HEIF

#ifdef DEC_FLIF
	{WITH_LEN("FLIF"), fmt_flif},
#endif // DEC_FLIF

#ifdef DEC_SVG
/* No magic bytes for SVG(Z) because it's either XML or XML inside gzip, and
 * XML requires that an XML parser parses the XML successfully to know if it
 * is XML. */
#endif // DEC_SVG
};
static const size_t MIN_MAGIC_LEN = 2; // Anything shorter is meaningless

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

#ifdef DEC_PCX
	{"dcx", -1},
	{"pcc", -1},
	{"pcx", -1},
#endif // DEC_PCX

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

#ifdef DEC_PNG
	{"png", -1},
#endif // DEC_PNG

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

#ifdef DEC_GIF
	{"gif", -1},
	{"gif87", -1},
	{"gif89", -1},
#endif // DEC_GIF

#ifdef DEC_TIFF
	{"tif", -1},
	{"tiff", -1},
#ifndef DEC_RAW
	{"cr2", -1},
	{"dng", -1},
	{"nef", -1},
#endif // DEC_RAW
#endif // DEC_TIFF

#ifdef DEC_RAW
	{"cr2", fmt_raw},
	{"dng", fmt_raw},
	{"nef", fmt_raw},
	{"orf", -1},
	{"raw", -1},
	{"rw2", -1},
#endif

#ifdef DEC_JPEG2000
	{"j2k", -1},
	{"jp2", -1},
	{"jpc", -1},
#endif // DEC_JPEG2000

#ifdef DEC_WEBP
	{"webp", -1},
#endif // DEC_WEBP

#ifdef DEC_HEIF
	{"heic", -1},
	{"heif", -1},
#endif // DEC_HEIF

#ifdef DEC_FLIF
	{"flif", -1},
#endif // DEC_FLIF

#ifdef DEC_SVG
	{"svg", fmt_svg},
	{"svgz", fmt_svg},
#endif // DEC_SVG
};


static int fextcmp(const void *restrict e1, const void *restrict e2) {
	const struct file_ext *restrict ext1 = e1;
	const struct file_ext *restrict ext2 = e2;
	return memcmp(ext1->ext, ext2->ext, sizeof(ext1->ext));
}

static int fbytescmp(const void *restrict m1, const void *restrict m2) {
	const struct file_bytes *restrict magic1 = m1;
	const struct file_bytes *restrict magic2 = m2;
	const size_t min_len = umin(magic1->len, magic2->len);
	return memcmp(magic1->bytes, magic2->bytes, min_len);
}

static const struct file_ext * search_extension(const char *filename,
const size_t len) {
	struct file_ext fext = {0};
	const size_t start = len - zumin(len, sizeof(fext.ext) + 1 /* dot */);
	const char *ext = memchr(filename + start, '.', sizeof(fext.ext));
	if (!ext) {
		return NULL;
	}
	++ext;

	const size_t ext_len = strlen(ext);
	if (ext_len) {
		for (size_t i = 0; i < ext_len; ++i) {
			fext.ext[i] = (char)tolower(ext[i]);
		}
		return bsearch(&fext, extension_map, ARRAY_LEN(extension_map),
			sizeof(*extension_map), fextcmp);
	}
	return NULL;
}

static const struct file_bytes * search_magic(FILE *ifp) {
	struct file_bytes in = {0};
	in.len = (unsigned int)fread(in.bytes, 1, sizeof(in.bytes), ifp);

	if (in.len < MIN_MAGIC_LEN) {
		return NULL;
	}
	return bsearch(&in, magic_map, ARRAY_LEN(magic_map),
		sizeof(*magic_map), fbytescmp);
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
		enum format_id id = dec->id;
		const magic_verify_t verify = format_map[id].verify;
		if (verify && (*verify)(ifp) == false) {
			return fmt_unknown;
		}
		return id;
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

static enum format_id open_image(FILE **outfp, const char *filename) {
	FILE *ifp;
	if (*outfp) {
		ifp = *outfp;
	} else {
		errno = 0;
		ifp = fopen(filename, "rb");
		if (!ifp) {
			return wu_open_error;
		}
	}

	errno = 0;
	const enum format_id id = find_decoder(ifp, filename);
	if (id != fmt_unknown) {
		rewind(ifp);
		*outfp = ifp;
	}
	return id;
}


enum wu_error callback_image(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	const enum format_id id = infile->fmt_id;
	return format_map[id].callback(infile, wuconf, state, event);
}

enum wu_error decode_image(struct image_file *infile,
const struct wu_conf *wuconf, const char *filename) {
	errno = 0;
	const enum format_id id = open_image(&infile->ifp, filename);
	if (id == fmt_unknown) {
		if (errno) {
			infile->err_msg = strdup(strerror(errno));
			return wu_open_error;
		}
		return wu_unknown_file_type;
	}

	char *str;
	size_t _len;
	infile->meta.fp = open_memstream(&str, &_len);

	fputs("Format: ", infile->meta.fp);
	fputs(format_map[id].name, infile->meta.fp);
	fputc('\n', infile->meta.fp);

	infile->fmt_id = id;
	const enum wu_error result = format_map[id].dec(infile, wuconf);
	fclose(infile->meta.fp);
	infile->meta.str = str;

	if (result == wu_ok) {
		normalize_sub_images(infile);
	}
	return result;
}

void free_file_list(struct file_list *files) {
	for (size_t i = 0; i < files->nr; ++i) {
		free(files->name[i]);
	}
	free(files);
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

struct file_list * filter_directory(const char *dirname, const char *init_name) {
	const size_t dir_len = strlen(dirname);
	DIR *dir = opendir(dir_len ? dirname : ".");
	if (!dir) {
		return NULL;
	}

	size_t nr = 16;
	struct file_list *files = flex_malloc(sizeof(*files), nr,
		sizeof(*files->name));
	if (!files) {
		closedir(dir);
		return NULL;
	}

	size_t init_name_len;
	size_t idx = 0;
	if (init_name) {
		init_name_len = strlen(init_name);
		files->name[idx] = pathcat(dirname, dir_len, init_name,
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
			if (idx == nr) {
				nr += nr / 4;
				void *hold = flex_realloc(files, sizeof(*files),
					nr, sizeof(*files->name));
				if (!hold) {
					break;
				}
				files = hold;
			}

			files->name[idx] = pathcat(dirname, dir_len,
				entry->d_name, name_len);
			if (files->name[idx]) {
				++idx;
			}
		}
	}
	closedir(dir);
	files->nr = idx;
	if (idx == 0) {
		free_file_list(files);
		return NULL;
	}
	return files;
}

void sort_dec_tables(void) {
	qsort(magic_map, ARRAY_LEN(magic_map), sizeof(magic_map[0]), fbytescmp);
	qsort(extension_map, ARRAY_LEN(extension_map), sizeof(extension_map[0]), fextcmp);
}
