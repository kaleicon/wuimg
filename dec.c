#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>

#include "wudefs.h"
#include "common.h"
#include "dec.h"

#include "dec_bmp.h"
#include "dec_pi.h"
#include "dec_pnm.h"
#include "dec_sgi.h"
#include "dec_sun.h"
#include "dec_tga.h"
#include "dec_wbmp.h"

#include "dec_flif.h"
#include "dec_gif.h"
#include "dec_heif.h"
#include "dec_jpeg.h"
#include "dec_jpeg2000.h"
#include "dec_png.h"
#include "dec_svg.h"
#include "dec_tiff.h"
#include "dec_webp.h"

struct file_magic {
	size_t len;
	const unsigned char *bytes;
	bool (*verify)(FILE *);
	dec_func_t func;
};

struct file_extension {
	const char *ext;
	dec_func_t func;
};

enum file_formats {
#include "dec.def"
	nb_of_fmt
};

/* The obvious issue here is that a short sequence that matches the start of a
 * longer one will ruin searches. */
static const size_t SAFE_MAX_MAGIC_LEN = 24;
static size_t MAX_MAGIC_LEN = 0;
static size_t MIN_MAGIC_LEN = SIZE_MAX; // Set at runtime

#define WITH_LEN(bytes) (sizeof(bytes) - 1), (const unsigned char *)bytes
static struct file_magic magic_map[] = {
#ifdef DEC_BMP
	{WITH_LEN("BM"), NULL, bmp_dec},
#endif // DEC_BMP

#ifdef DEC_PI
	{WITH_LEN("Pi"), NULL, pi_dec},
#endif // DEC_PI

#ifdef DEC_PNM
	{WITH_LEN("P1"), NULL, pnm_dec},
	{WITH_LEN("P2"), NULL, pnm_dec},
	{WITH_LEN("P3"), NULL, pnm_dec},
	{WITH_LEN("P4"), NULL, pnm_dec},
	{WITH_LEN("P5"), NULL, pnm_dec},
	{WITH_LEN("P6"), NULL, pnm_dec},
	{WITH_LEN("P7\n"), NULL, pnm_dec}, // PAM
	{WITH_LEN("P7 332\n"), NULL, pnm_dec}, // Xv thumbnail
#endif // DEC_PNM

#ifdef DEC_SGI
	{WITH_LEN("\x01\xda"), NULL, sgi_dec},
#endif // DEC_SGI

#ifdef DEC_SUN
	{WITH_LEN("\x59\xa6\x6a\x95"), NULL, sun_dec},
#endif // DEC_SUN

#ifdef DEC_PNG
	{WITH_LEN("\x89PNG\r\n\x1a\n"), NULL, png_dec},
#endif // DEC_PNG

#ifdef DEC_JPEG
	{WITH_LEN("\xff\xd8\xff"), NULL, jpeg_dec},
#endif // DEC_JPEG

#ifdef DEC_GIF
	{WITH_LEN("GIF87a"), NULL, gif_dec},
	{WITH_LEN("GIF89a"), NULL, gif_dec},
#endif // DEC_GIF

#ifdef DEC_TIFF
	{WITH_LEN("II*\0"), NULL, tiff_dec},
	{WITH_LEN("MM\0*"), NULL, tiff_dec},
#endif // DEC_TIFF

#ifdef DEC_JPEG2000
	{WITH_LEN("\0\0\0\x0c\x6a\x50\x20\x20\x0d\0a\x87\x0a"), NULL, jp2_dec},
	{WITH_LEN("\x0d\x0a\x87\x0a"), NULL, jp2_dec},
	{WITH_LEN("\xff\x4f\xff\x51"), NULL, j2k_dec},
#endif // DEC_JPEG2000

#ifdef DEC_WEBP
	// Various formats may start with "RIFF", notably WAV.
	{WITH_LEN("RIFF"), webp_verify, webp_dec},
#endif // DEC_WEBP

#ifdef DEC_HEIF
	// Split string to prevent 'hex escape sequence out of range' error.
	{WITH_LEN("\0\0\0\x18" "ftypheic"), NULL, heif_dec},
#endif // DEC_HEIF

#ifdef DEC_FLIF
	{WITH_LEN("FLIF"), NULL, flif_dec},
#endif // DEC_FLIF

#ifdef DEC_SVG
/* No magic bytes for SVG(Z) because it's either XML or XML inside gzip, and
 * XML requires that an XML parser parses the XML successfully to know if it
 * was XML. */
#endif // DEC_SVG
};

// File extensions for filtering and fallbacks
static const size_t SAFE_MAX_EXT_LEN = 8;
static size_t MAX_EXT_LEN = 0; // Set at runtime
static struct file_extension extension_map[] = {
#ifdef DEC_BMP
	{"bmp", bmp_dec},
#endif // DEC_BMP

#ifdef DEC_PI
	{"g", pi_dec},
	{"pi", pi_dec},
#endif // DEC_PI

#ifdef DEC_PNM
	{"mtv", pnm_dec},
	{"pbm", pnm_dec},
	{"pgm", pnm_dec},
	{"ppm", pnm_dec},
	{"pam", pnm_dec},
	{"pnm", pnm_dec},
	{"p7", pnm_dec},
#endif // DEC_PNM

#ifdef DEC_SGI
	{"bw", sgi_dec},
	{"rgb", sgi_dec},
	{"rgba", sgi_dec},
	{"sgi", sgi_dec},
#endif // DEC_SGI

#ifdef DEC_SUN
	{"im1", sun_dec},
	{"im4", sun_dec},
	{"im8", sun_dec},
	{"im24", sun_dec},
	{"im32", sun_dec},
	{"ras", sun_dec},
	{"sun", sun_dec},
#endif // DEC_SUN

#ifdef DEC_TGA
	{"tga", tga_dec},
#endif // DEC_TGA

#ifdef DEC_WBMP
	{"wbmp", wbmp_dec},
#endif // DEC_WBMP

#ifdef DEC_PNG
	{"png", png_dec},
#endif // DEC_PNG

#ifdef DEC_JPEG
	{"jfi", jpeg_dec},
	{"jfif", jpeg_dec},
	{"jif", jpeg_dec},
	{"jpe", jpeg_dec},
	{"jpeg", jpeg_dec},
	{"jpg", jpeg_dec},
	{"jps", jpeg_dec},
	{"mpo", jpeg_dec},
	{"thm", jpeg_dec},
#endif // DEC_JPEG

#ifdef DEC_GIF
	{"gif", gif_dec},
	{"gif87", gif_dec},
	{"gif89", gif_dec},
#endif // DEC_GIF

#ifdef DEC_TIFF
	{"tif", tiff_dec},
	{"tiff", tiff_dec},
#endif // DEC_TIFF

#ifdef DEC_JPEG2000
	{"j2k", j2k_dec},
	{"jp2", jp2_dec},
	{"jpc", j2k_dec},
#endif // DEC_JPEG2000

#ifdef DEC_WEBP
	{"webp", webp_dec},
#endif // DEC_WEBP

#ifdef DEC_HEIF
	{"heic", heif_dec},
	{"heif", heif_dec},
#endif // DEC_HEIF

#ifdef DEC_FLIF
	{"flif", flif_dec},
#endif // DEC_FLIF

#ifdef DEC_SVG
	{"svg", svg_dec},
	{"svgz", svg_dec},
#endif // DEC_SVG
};


static int fmagiccmp(const void *restrict f1, const void *restrict f2) {
	const struct file_magic *restrict magic1 = (struct file_magic *)f1;
	const struct file_magic *restrict magic2 = (struct file_magic *)f2;
	const size_t min_len = zumin(magic1->len, magic2->len);
	return memcmp(magic1->bytes, magic2->bytes, min_len);
}

static int fextcmp(const void *restrict f1, const void *restrict f2) {
	return strcmp(
		((const struct file_extension *)f1)->ext,
		((const struct file_extension *)f2)->ext);
}

static const struct file_extension * search_extension(const char *ext) {
	const size_t ext_len = strlen(ext);
	if (ext_len == 0 || ext_len > MAX_EXT_LEN) {
		return NULL;
	}

	char case_ext[SAFE_MAX_EXT_LEN];
	for (size_t i = 0; i < ext_len; ++i) {
		case_ext[i] = (char)tolower(ext[i]);
	}
	case_ext[ext_len] = 0;

	const struct file_extension fext = {
		.ext = case_ext,
	};

	return bsearch(&fext, extension_map, ARRAY_LEN(extension_map),
		sizeof(extension_map[0]), fextcmp);
}

static const struct file_magic * search_magic(FILE *ifp) {
	unsigned char signature[SAFE_MAX_MAGIC_LEN];
	const struct file_magic dummy = {
		.len = fread(signature, 1, MAX_MAGIC_LEN, ifp),
		.bytes = signature,
	};

	if (dummy.len < MIN_MAGIC_LEN) {
		return NULL;
	}
	return bsearch(&dummy, magic_map, ARRAY_LEN(magic_map),
		sizeof(magic_map[0]), fmagiccmp);
}

static dec_func_t try_extension(const char *filename) {
	const char *ext = strrchr(filename, '.');
	if (ext) {
		const struct file_extension *dec = search_extension(ext + 1);
		if (dec) {
			return dec->func;
		}
	}
	return NULL;
}

static dec_func_t try_magic(FILE *ifp) {
	const struct file_magic *dec = search_magic(ifp);
	if (dec) {
		if (dec->verify == NULL) {
			return dec->func;
		} else if ((*dec->verify)(ifp) == true) {
			return dec->func;
		}
	}
	return NULL;
}

static dec_func_t find_decoder(FILE *ifp, const char *filename) {
	errno = 0;
	const dec_func_t func = try_magic(ifp);
	if (errno) {
		return NULL;
	}
	rewind(ifp);
	if (func) {
		return func;
	}
	return try_extension(filename);
}

enum wu_error decode_image(struct image_file *infile,
const struct wu_conf *wuconf, const char *filename) {
	errno = 0;
	infile->ifp = fopen(filename, "rb");
	if (!infile->ifp) {
		infile->err_msg = strdup(strerror(errno));
		return wu_open_error;
	}

	errno = 0;
	const dec_func_t func = find_decoder(infile->ifp, filename);
	if (!func) {
		if (errno) {
			infile->err_msg = strdup(strerror(errno));
			return wu_open_error;
		}
		return wu_unknown_file_type;
	}

	enum wu_error result = (*func)(infile, wuconf);
	if (result == wu_ok && infile->sub_img) {
		normalize_sub_images(infile);
	}
	return result;
}

static char * pathcat(const char *p1, const size_t p1_len, const char *p2,
const size_t p2_len) {
	// Assumes p1 has a trailing slash
	char *cat = malloc(p1_len + p2_len + 1);
	memcpy(cat, p1, p1_len);
	memcpy(cat + p1_len, p2, p2_len + 1);
	return cat;
}

char ** filter_images(const char *dirname, const char *init_name,
size_t *nr_of_entries) {
	DIR *dir = opendir(dirname[0] == '\0' ? "." : dirname);
	if (!dir) {
		return NULL;
	}

	const size_t dir_len = strlen(dirname);
	const size_t init_name_len = init_name ? strlen(init_name) : 0;

	size_t size = 16;
	char **filelist = malloc(size * sizeof(char *));
	size_t idx = 0;
	if (init_name) {
		filelist[idx] = pathcat(dirname, dir_len, init_name,
			init_name_len);
		++idx;
	}

	bool found_init = false;
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
		if (!found_init && name_len == init_name_len) {
			if (!memcmp(init_name, entry->d_name, name_len)) {
				found_init = true;
				continue;
			}
		}
		const size_t chr_off = name_len - zumin(name_len, MAX_EXT_LEN+1);
		const char *ext = strchr(entry->d_name + chr_off, '.');
		if (!ext) {
			continue;
		}

		const struct file_extension *dec = search_extension(ext + 1);
		if (dec) {
			if (idx == size) {
				size += size / 4;
				void *hold = realloc(filelist,
					size * sizeof(filelist[0]));
				if (!hold) {
					free(filelist);
					return NULL;
				}
				filelist = hold;
			}

			filelist[idx] = pathcat(dirname, dir_len,
				entry->d_name, name_len);
			++idx;
		}
	}
	closedir(dir);

	if (idx == 0) {
		free(filelist);
		return NULL;
	}
	*nr_of_entries = idx;
	return realloc(filelist, idx * sizeof(filelist[0]));
}

void sort_dec_tables(void) {
	qsort(magic_map, ARRAY_LEN(magic_map), sizeof(magic_map[0]), fmagiccmp);
	qsort(extension_map, ARRAY_LEN(extension_map), sizeof(extension_map[0]), fextcmp);

	for (size_t i = 0; i < ARRAY_LEN(extension_map); ++i) {
		MAX_EXT_LEN = zumax(strlen(extension_map[i].ext), MAX_EXT_LEN);
	}

	for (size_t i = 0; i < ARRAY_LEN(magic_map); ++i) {
		MIN_MAGIC_LEN = zumin(magic_map[i].len, MIN_MAGIC_LEN);
		MAX_MAGIC_LEN = zumax(magic_map[i].len, MAX_MAGIC_LEN);
	}
}
