#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "wudefs.h"
#include "dec.h"
#include "dec_pi.h"
#include "dec_netpbm.h"
#include "dec_flif.h"
#include "dec_gif.h"
#include "dec_heif.h"
#include "dec_jpeg.h"
#include "dec_jpeg2000.h"
#include "dec_png.h"
#include "dec_tiff.h"
#include "dec_webp.h"

#define MAX_EXT_LEN 5

struct file_decoder {
	const char *ext;
	enum wu_error_type (*func)(struct image_file *);
} decoder_array[] = {
	{"flif", flif_dec},
	{"g", pi_dec},
	{"gif", gif_dec},
	{"heic", heif_dec},
	{"heif", heif_dec},
	{"j2k", jpeg2000_dec},
	{"jp2", jpeg2000_dec},
	{"jpeg", jpeg_dec},
	{"jpg", jpeg_dec},
	{"mpo", jpeg_dec},
	{"pbm", netpbm_dec},
	{"pgm", netpbm_dec},
	{"png", png_dec},
	{"pnm", netpbm_dec},
	{"ppm", netpbm_dec},
	{"tif", tiff_dec},
	{"tiff", tiff_dec},
	{"webp", webp_dec},
};


enum wu_error_type decode_image(struct image_file *infile,
struct file_class *entry) {
	infile->name = entry->name;
	if (entry->func) {
		enum wu_error_type result = entry->func(infile);
		if (result == wu_ok) {
			normalize_sub_images(infile);
		}
		return result;
	}
	return wu_unknown_file_type;
}

static int compcasefdec(const void *restrict key, const void *restrict f) {
	return strncasecmp((const char *)key,
		((const struct file_decoder *)f)->ext, MAX_EXT_LEN);
}

#define NR_OF_DECS (sizeof (decoder_array) / sizeof (decoder_array[0]))
struct file_class * find_images(const char *dirname, size_t *nr_of_entries) {
	DIR *dir;
	size_t dir_len = 0;
	if (dirname) {
		dir = opendir(dirname);
		dir_len = strlen(dirname);
	} else {
		dir = opendir("./");
	}
	if (!dir) {
		return NULL;
	}

	size_t nr = 0;
	size_t size = 8;
	struct file_class *filelist = malloc(size * sizeof(struct file_class));
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
		const size_t ext_start = name_len - MAX_EXT_LEN;
		const char *ext = memchr(entry->d_name + ext_start, '.',
			MAX_EXT_LEN);

		if (!ext) {
			continue;
		}

		struct file_decoder *res = bsearch(ext + 1, decoder_array,
			NR_OF_DECS, sizeof(struct file_decoder), compcasefdec);

		if (res) {
			if (nr == size) {
				size += size / 2;
				filelist = realloc(filelist,
					size * sizeof(struct file_class));
			}

			filelist[nr].name = malloc(dir_len + name_len + 1);
			if (dirname) {
				memcpy(filelist[nr].name, dirname, dir_len);
			}
			memcpy(filelist[nr].name + dir_len, entry->d_name,
				name_len + 1);

			filelist[nr].func = res->func;
			++nr;
		}
	}
	closedir(dir);
	if (nr == 0) {
		free(filelist);
		return NULL;
	}
	*nr_of_entries = nr;
	return realloc(filelist, nr * sizeof(struct file_class));
}
